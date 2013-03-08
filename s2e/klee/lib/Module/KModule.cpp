//===-- KModule.cpp -------------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

// FIXME: This does not belong here.
#include "klee/Common.h"

#include "klee/Internal/Module/KModule.h"

#include "Passes.h"

#include "klee/Interpreter.h"
#include "klee/Internal/Module/Cell.h"
#include "klee/Internal/Module/KInstruction.h"
#include "klee/Internal/Module/InstructionInfoTable.h"
#include "klee/Internal/Support/ModuleUtil.h"

#include "llvm/Bitcode/ReaderWriter.h"
#include "llvm/Instructions.h"
#if !(LLVM_VERSION_MAJOR == 2 && LLVM_VERSION_MINOR < 7)
#include "llvm/LLVMContext.h"
#endif
#include "llvm/Module.h"
#include "llvm/ModuleProvider.h"
#include "llvm/PassManager.h"
#include "llvm/ValueSymbolTable.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"
#if !(LLVM_VERSION_MAJOR == 2 && LLVM_VERSION_MINOR < 7)
#include "llvm/Support/raw_os_ostream.h"
#endif
#include "llvm/System/Path.h"
#include "llvm/Target/TargetData.h"
#include "llvm/Transforms/Scalar.h"

#include <sstream>

using namespace llvm;
using namespace klee;

namespace {
  enum SwitchImplType {
    eSwitchTypeSimple,
    eSwitchTypeLLVM,
    eSwitchTypeInternal
  };

  cl::list<std::string>
  MergeAtExit("merge-at-exit");
    
  cl::opt<bool>
  NoTruncateSourceLines("no-truncate-source-lines",
                        cl::desc("Don't truncate long lines in the output source"));

  cl::opt<bool>
  OutputSource("output-source",
               cl::desc("Write the assembly for the final transformed source"),
               cl::init(true));

  cl::opt<bool>
  OutputModule("output-module",
               cl::desc("Write the bitcode for the final transformed module"),
               cl::init(false));

  cl::opt<SwitchImplType>
  SwitchType("switch-type", cl::desc("Select the implementation of switch"),
             cl::values(clEnumValN(eSwitchTypeSimple, "simple", 
                                   "lower to ordered branches"),
                        clEnumValN(eSwitchTypeLLVM, "llvm", 
                                   "lower using LLVM"),
                        clEnumValN(eSwitchTypeInternal, "internal", 
                                   "execute switch internally"),
                        clEnumValEnd),
             cl::init(eSwitchTypeInternal));
  
  cl::opt<bool>
  DebugPrintEscapingFunctions("debug-print-escaping-functions", 
                              cl::desc("Print functions whose address is taken."));
}

namespace llvm {
extern void CreateOptimizePasses(PassManagerBase&, Module*);
}

namespace klee {

struct KModulePrivate {
  llvm::ExistingModuleProvider moduleProvider;
  llvm::PassManager pmOptimize, pm3, pm4;
  llvm::FunctionPassManager fpmOptimize, fpm3, fpm4;

  KModulePrivate(llvm::Module *module,
                 llvm::TargetData *targetData)
          : moduleProvider(module),
            fpmOptimize(&moduleProvider),
            fpm3(&moduleProvider),
            fpm4(&moduleProvider) {

    pm3.add(createCFGSimplificationPass());
    fpm3.add(createCFGSimplificationPass());

    switch(SwitchType) {
    case eSwitchTypeInternal: break;
    case eSwitchTypeSimple:
      pm3.add(new LowerSwitchPass());
      fpm3.add(new LowerSwitchPass());
      break;
    case eSwitchTypeLLVM:
      pm3.add(createLowerSwitchPass()); break;
      fpm3.add(createLowerSwitchPass()); break;
    default: klee_error("invalid --switch-type");
    }
    pm3.add(new IntrinsicCleanerPass(*targetData));
    fpm3.add(new IntrinsicFunctionCleanerPass());

    //pm3.add(new PhiCleanerPass());

    pm4.add(new PhiCleanerPass());
    fpm4.add(new PhiCleanerPass());

    CreateOptimizePasses(pmOptimize, module);

    CreateOptimizePasses(fpmOptimize, module);
    fpmOptimize.doInitialization();
  }

  ~KModulePrivate() {
    moduleProvider.releaseModule();
  }
};

} // namespace klee

KModule::KModule(Module *_module) 
  : module(_module),
    targetData(new TargetData(module)),
    dbgStopPointFn(0),
    kleeMergeFn(0),
    infos(0),
    p(new KModulePrivate(_module, targetData)) {
}

KModule::~KModule() {
  delete infos;

  for (std::vector<KFunction*>::iterator it = functions.begin(), 
         ie = functions.end(); it != ie; ++it)
    delete *it;

  delete targetData;
  delete module;

  delete p;
}

/***/

// what a hack
static Function *getStubFunctionForCtorList(Module *m,
                                            GlobalVariable *gv, 
                                            std::string name) {
  assert(!gv->isDeclaration() && !gv->hasInternalLinkage() &&
         "do not support old LLVM style constructor/destructor lists");
  
  std::vector<const Type*> nullary;

  Function *fn = Function::Create(FunctionType::get(Type::getVoidTy(getGlobalContext()), 
						    nullary, false),
				  GlobalVariable::InternalLinkage, 
				  name,
                              m);
  BasicBlock *bb = BasicBlock::Create(getGlobalContext(), "entry", fn);
  
  // From lli:
  // Should be an array of '{ int, void ()* }' structs.  The first value is
  // the init priority, which we ignore.
  ConstantArray *arr = dyn_cast<ConstantArray>(gv->getInitializer());
  if (arr) {
    for (unsigned i=0; i<arr->getNumOperands(); i++) {
      ConstantStruct *cs = cast<ConstantStruct>(arr->getOperand(i));
      assert(cs->getNumOperands()==2 && "unexpected element in ctor initializer list");
      
      Constant *fp = cs->getOperand(1);      
      if (!fp->isNullValue()) {
        if (llvm::ConstantExpr *ce = dyn_cast<llvm::ConstantExpr>(fp))
          fp = ce->getOperand(0);

        if (Function *f = dyn_cast<Function>(fp)) {
	  CallInst::Create(f, "", bb);
        } else {
          assert(0 && "unable to get function pointer from ctor initializer list");
        }
      }
    }
  }
  
  ReturnInst::Create(getGlobalContext(), bb);

  return fn;
}

static void injectStaticConstructorsAndDestructors(Module *m) {
  GlobalVariable *ctors = m->getNamedGlobal("llvm.global_ctors");
  GlobalVariable *dtors = m->getNamedGlobal("llvm.global_dtors");
  
  if (ctors || dtors) {
    Function *mainFn = m->getFunction("main");
    assert(mainFn && "unable to find main function");

    if (ctors)
    CallInst::Create(getStubFunctionForCtorList(m, ctors, "klee.ctor_stub"),
		     "", mainFn->begin()->begin());
    if (dtors) {
      Function *dtorStub = getStubFunctionForCtorList(m, dtors, "klee.dtor_stub");
      for (Function::iterator it = mainFn->begin(), ie = mainFn->end();
           it != ie; ++it) {
        if (isa<ReturnInst>(it->getTerminator()))
	  CallInst::Create(dtorStub, "", it->getTerminator());
      }
    }
  }
}

static void forceImport(Module *m, const char *name, const Type *retType, ...) {
  // If module lacks an externally visible symbol for the name then we
  // need to create one. We have to look in the symbol table because
  // we want to check everything (global variables, functions, and
  // aliases).

  Value *v = m->getValueSymbolTable().lookup(name);
  GlobalValue *gv = dyn_cast_or_null<GlobalValue>(v);

  if (!gv || gv->hasInternalLinkage()) {
    va_list ap;

    va_start(ap, retType);
    std::vector<const Type *> argTypes;
    while (const Type *t = va_arg(ap, const Type*))
      argTypes.push_back(t);
    va_end(ap);

    m->getOrInsertFunction(name, FunctionType::get(retType, argTypes, false));
  }
}

#ifdef __MINGW32__
static const char *index(const char *str, char c) {
  while(*str) {
    if (*str == c) {
      return str;
    }
    ++str;
  }
  return NULL;
}
#endif

void KModule::prepare(const Interpreter::ModuleOptions &opts,
                      InterpreterHandler *ih) {
  if (!MergeAtExit.empty()) {
    Function *mergeFn = module->getFunction("klee_merge");
    if (!mergeFn) {
      const llvm::FunctionType *Ty = 
        FunctionType::get(Type::getVoidTy(getGlobalContext()), 
                          std::vector<const Type*>(), false);
      mergeFn = Function::Create(Ty, GlobalVariable::ExternalLinkage,
				 "klee_merge",
				 module);
    }

    for (cl::list<std::string>::iterator it = MergeAtExit.begin(), 
           ie = MergeAtExit.end(); it != ie; ++it) {
      std::string &name = *it;
      Function *f = module->getFunction(name);
      if (!f) {
        klee_error("cannot insert merge-at-exit for: %s (cannot find)",
                   name.c_str());
      } else if (f->isDeclaration()) {
        klee_error("cannot insert merge-at-exit for: %s (external)",
                   name.c_str());
      }

      BasicBlock *exit = BasicBlock::Create(getGlobalContext(), "exit", f);
      PHINode *result = 0;
      if (f->getReturnType() != Type::getVoidTy(getGlobalContext()))
        result = PHINode::Create(f->getReturnType(), "retval", exit);
      CallInst::Create(mergeFn, "", exit);
      ReturnInst::Create(getGlobalContext(), result, exit);

      llvm::errs() << "KLEE: adding klee_merge at exit of: " << name << "\n";
      for (llvm::Function::iterator bbit = f->begin(), bbie = f->end(); 
           bbit != bbie; ++bbit) {
        if (&*bbit != exit) {
          Instruction *i = bbit->getTerminator();
          if (i->getOpcode()==Instruction::Ret) {
            if (result) {
              result->addIncoming(i->getOperand(0), bbit);
            }
            i->eraseFromParent();
	    BranchInst::Create(exit, bbit);
          }
        }
      }
    }
  }

  // Inject checks prior to optimization... we also perform the
  // invariant transformations that we will end up doing later so that
  // optimize is seeing what is as close as possible to the final
  // module.
  PassManager pm;
  pm.add(new RaiseAsmPass());
  if (opts.CheckDivZero) pm.add(new DivCheckPass());
  // FIXME: This false here is to work around a bug in
  // IntrinsicLowering which caches values which may eventually be
  // deleted (via RAUW). This can be removed once LLVM fixes this
  // issue.
  pm.add(new IntrinsicCleanerPass(*targetData, false));
  pm.run(*module);

  if (opts.Optimize)
    p->pmOptimize.run(*module);


  // Force importing functions required by intrinsic lowering. Kind of
  // unfortunate clutter when we don't need them but we won't know
  // that until after all linking and intrinsic lowering is
  // done. After linking and passes we just try to manually trim these
  // by name. We only add them if such a function doesn't exist to
  // avoid creating stale uses.

  const llvm::Type *i8Ty = Type::getInt8Ty(getGlobalContext());
  forceImport(module, "memcpy", PointerType::getUnqual(i8Ty),
              PointerType::getUnqual(i8Ty),
              PointerType::getUnqual(i8Ty),
              targetData->getIntPtrType(getGlobalContext()), (Type*) 0);
  forceImport(module, "memmove", PointerType::getUnqual(i8Ty),
              PointerType::getUnqual(i8Ty),
              PointerType::getUnqual(i8Ty),
              targetData->getIntPtrType(getGlobalContext()), (Type*) 0);
  forceImport(module, "memset", PointerType::getUnqual(i8Ty),
              PointerType::getUnqual(i8Ty),
              Type::getInt32Ty(getGlobalContext()),
              targetData->getIntPtrType(getGlobalContext()), (Type*) 0);

  // FIXME: Missing force import for various math functions.

  // FIXME: Find a way that we can test programs without requiring
  // this to be linked in, it makes low level debugging much more
  // annoying.
  for(std::vector<std::string>::const_iterator it = opts.ExtraLibraries.begin(),
             ie = opts.ExtraLibraries.end(); it != ie; ++it) {
      module = linkWithLibrary(module, *it);
  }

  // Needs to happen after linking (since ctors/dtors can be modified)
  // and optimization (since global optimization can rewrite lists).
  injectStaticConstructorsAndDestructors(module);

  // Finally, run the passes that maintain invariants we expect during
  // interpretation. We run the intrinsic cleaner just in case we
  // linked in something with intrinsics but any external calls are
  // going to be unresolved. We really need to handle the intrinsics
  // directly I think?

#if 0
  PassManager pm3;
  pm3.add(createCFGSimplificationPass());
  switch(SwitchType) {
  case eSwitchTypeInternal: break;
  case eSwitchTypeSimple: pm3.add(new LowerSwitchPass()); break;
  case eSwitchTypeLLVM:  pm3.add(createLowerSwitchPass()); break;
  default: klee_error("invalid --switch-type");
  }
  pm3.add(new IntrinsicCleanerPass(*targetData));
  //pm3.add(new PhiCleanerPass());
#endif
  p->pm3.run(*module);

  if (opts.CustomPasses) {
      Module::iterator it;
      for(it = module->begin(); it != module->end(); ++it) {
          opts.CustomPasses->run(*it);
      }
  }

  //The PhiCleaner is important to be the last, because the rest of KLEE
  //makes assumptions about how PHI nodes are placed.
#if 0
  PassManager pm4;
  pm4.add(new PhiCleanerPass());
#endif
  p->pm4.run(*module);

  // For cleanliness see if we can discard any of the functions we
  // forced to import.
  Function *f;
  f = module->getFunction("memcpy");
  if (f && f->use_empty()) f->eraseFromParent();
  f = module->getFunction("memmove");
  if (f && f->use_empty()) f->eraseFromParent();
  f = module->getFunction("memset");
  if (f && f->use_empty()) f->eraseFromParent();


  // Write out the .ll assembly file. We truncate long lines to work
  // around a kcachegrind parsing bug (it puts them on new lines), so
  // that source browsing works.
  if (OutputSource) {
    std::ostream *os = ih->openOutputFile("assembly.ll");
    assert(os && os->good() && "unable to open source output");

#if (LLVM_VERSION_MAJOR == 2 && LLVM_VERSION_MINOR < 6)
    // We have an option for this in case the user wants a .ll they
    // can compile.
    if (NoTruncateSourceLines) {
      os << *module;
    } else {
      bool truncated = false;
      std::string string;
      llvm::raw_string_ostream rss(string);
      rss << *module;
      rss.flush();
      const char *position = string.c_str();

      for (;;) {
        const char *end = index(position, '\n');
        if (!end) {
          os << position;
          break;
        } else {
          unsigned count = (end - position) + 1;
          if (count<255) {
            os->write(position, count);
          } else {
            os->write(position, 254);
            os << "\n";
            truncated = true;
          }
          position = end+1;
        }
      }
    }
#else
    llvm::raw_os_ostream *ros = new llvm::raw_os_ostream(*os);

    // We have an option for this in case the user wants a .ll they
    // can compile.
    if (NoTruncateSourceLines) {
      *ros << *module;
    } else {
      bool truncated = false;
      std::string string;
      llvm::raw_string_ostream rss(string);
      rss << *module;
      rss.flush();
      const char *position = string.c_str();

      for (;;) {
        const char *end = index(position, '\n');
        if (!end) {
          *ros << position;
          break;
        } else {
          unsigned count = (end - position) + 1;
          if (count<255) {
            ros->write(position, count);
          } else {
            ros->write(position, 254);
            *ros << "\n";
            truncated = true;
          }
          position = end+1;
        }
      }
    }
    delete ros;
#endif

    delete os;
  }

  if (OutputModule) {
    std::ostream *f = ih->openOutputFile("final.bc");
    llvm::raw_os_ostream* rfs = new llvm::raw_os_ostream(*f);
    WriteBitcodeToFile(module, *rfs);
    delete rfs;
    delete f;
  }

  dbgStopPointFn = module->getFunction("llvm.dbg.stoppoint");
  kleeMergeFn = module->getFunction("klee_merge");

  /* Build shadow structures */

  infos = new InstructionInfoTable(module);  
  
  for (Module::iterator it = module->begin(), ie = module->end();
       it != ie; ++it) {
    if (it->isDeclaration())
      continue;

    KFunction *kf = new KFunction(it, this);
    
    for (unsigned i=0; i<kf->numInstructions; ++i) {
      KInstruction *ki = kf->instructions[i];
      ki->info = &infos->getInfo(ki->inst);
    }

    functions.push_back(kf);
    functionMap.insert(std::make_pair(it, kf));
  }

  /* Compute various interesting properties */

  for (std::vector<KFunction*>::iterator it = functions.begin(), 
         ie = functions.end(); it != ie; ++it) {
    KFunction *kf = *it;
    if (functionEscapes(kf->function))
      escapingFunctions.insert(kf->function);
  }

  if (DebugPrintEscapingFunctions && !escapingFunctions.empty()) {
    llvm::errs() << "KLEE: escaping functions: [";
    for (std::set<Function*>::iterator it = escapingFunctions.begin(), 
         ie = escapingFunctions.end(); it != ie; ++it) {
      llvm::errs() << (*it)->getName() << ", ";
    }
    llvm::errs() << "]\n";
  }
}

KFunction* KModule::updateModuleWithFunction(llvm::Function *f)
{
    assert(functionMap.find(f) == functionMap.end());

    //We should make sure that the new functions have
    //the correct invariants
    //IntrinsicCleanerPass ip(*targetData, false);
    //ip.runOnFunction(*f);

    p->fpmOptimize.run(*f);

    p->fpm3.run(*f);
    p->fpm4.run(*f);

    KFunction *kf = new KFunction(f, this);

    /* TODO: update InstructionInfoTable here */
    for (unsigned i=0; i<kf->numInstructions; ++i) {
      KInstruction *ki = kf->instructions[i];
      ki->info = &infos->getInfo(ki->inst);
    }

    functions.push_back(kf);
    functionMap.insert(std::make_pair(f, kf));

    if (functionEscapes(kf->function))
      escapingFunctions.insert(kf->function);

    return kf;
}

void KModule::removeFunction(llvm::Function *f)
{
    std::map<llvm::Function*, KFunction*>::iterator it = functionMap.find(f);
    assert(it != functionMap.end());

    KFunction* kf = it->second;
    functions.erase(std::find(functions.begin(), functions.end(), kf));
    escapingFunctions.erase(f);
    functionMap.erase(f);
    delete kf;

    f->eraseFromParent();
}

KConstant* KModule::getKConstant(Constant *c) {
  std::map<llvm::Constant*, KConstant*>::iterator it = constantMap.find(c);
  if (it != constantMap.end())
    return it->second;
  return NULL;
}

unsigned KModule::getConstantID(Constant *c, KInstruction* ki) {
  KConstant *kc = getKConstant(c);
  if (kc)
    return kc->id;  

  unsigned id = constants.size();
  kc = new KConstant(c, id, ki);
  constantMap.insert(std::make_pair(c, kc));
  constants.push_back(c);
  return id;
}

/***/

KConstant::KConstant(llvm::Constant* _ct, unsigned _id, KInstruction* _ki) {
  ct = _ct;
  id = _id;
  ki = _ki;
}

/***/

KFunction::KFunction(llvm::Function *_function,
                     KModule *km) 
  : function(_function),
    numArgs(function->arg_size()),
    numInstructions(0),
    trackCoverage(true) {
  for (llvm::Function::iterator bbit = function->begin(), 
         bbie = function->end(); bbit != bbie; ++bbit) {
    BasicBlock *bb = bbit;
    basicBlockEntry[bb] = numInstructions;
    numInstructions += bb->size();
  }

  instructions = new KInstruction*[numInstructions];

  std::map<Instruction*, unsigned> registerMap;

  // The first arg_size() registers are reserved for formals.
  unsigned rnum = numArgs;
  for (llvm::Function::iterator bbit = function->begin(), 
         bbie = function->end(); bbit != bbie; ++bbit) {
    for (llvm::BasicBlock::iterator it = bbit->begin(), ie = bbit->end();
         it != ie; ++it)
      registerMap[it] = rnum++;
  }
  numRegisters = rnum;
  
  unsigned i = 0;
  for (llvm::Function::iterator bbit = function->begin(), 
         bbie = function->end(); bbit != bbie; ++bbit) {
    for (llvm::BasicBlock::iterator it = bbit->begin(), ie = bbit->end();
         it != ie; ++it) {
      KInstruction *ki;

      switch(it->getOpcode()) {
      case Instruction::GetElementPtr:
        ki = new KGEPInstruction(); break;
      default:
        ki = new KInstruction(); break;
      }

      unsigned numOperands = it->getNumOperands();
      ki->inst = it;      
      ki->operands = new int[numOperands];
      ki->dest = registerMap[it];
      for (unsigned j=0; j<numOperands; j++) {
        Value *v = it->getOperand(j);
        
        if (Instruction *inst = dyn_cast<Instruction>(v)) {
          ki->operands[j] = registerMap[inst];
        } else if (Argument *a = dyn_cast<Argument>(v)) {
          ki->operands[j] = a->getArgNo();
        } else if (isa<BasicBlock>(v) || isa<InlineAsm>(v) ||
                   isa<MDNode>(v)) {
          ki->operands[j] = -1;
        } else {
          assert(isa<Constant>(v));
          Constant *c = cast<Constant>(v);
          ki->operands[j] = -(km->getConstantID(c, ki) + 2);
        }
      }

      instructions[i++] = ki;
    }
  }
}

KFunction::~KFunction() {
  for (unsigned i=0; i<numInstructions; ++i)
    delete instructions[i];
  delete[] instructions;
}