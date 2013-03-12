//===-- Constraints.cpp ---------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "klee/Constraints.h"

#include "klee/util/ExprPPrinter.h"
#include "klee/util/ExprVisitor.h"

#include <iostream>
#include <map>

using namespace klee;

class ExprReplaceVisitor : public ExprVisitor {
private:
  ref<Expr> src, dst;

public:
  ExprReplaceVisitor(ref<Expr> _src, ref<Expr> _dst) : src(_src), dst(_dst) {}

  Action visitExpr(const Expr &e) {
    if (e == *src.get()) {
      return Action::changeTo(dst);
    } else {
      return Action::doChildren();
    }
  }

  Action visitExprPost(const Expr &e) {
    if (e == *src.get()) {
      return Action::changeTo(dst);
    } else {
      return Action::doChildren();
    }
  }
};

class ExprReplaceVisitor2 : public ExprVisitor {
private:
  const std::map< ref<Expr>, ref<Expr> > &replacements;

public:
  ExprReplaceVisitor2(const std::map< ref<Expr>, ref<Expr> > &_replacements) 
    : ExprVisitor(true),
      replacements(_replacements) {}

  Action visitExprPost(const Expr &e) {
    std::map< ref<Expr>, ref<Expr> >::const_iterator it =
      replacements.find(ref<Expr>((Expr*) &e));
    if (it!=replacements.end()) {
      return Action::changeTo(it->second);
    } else {
      return Action::doChildren();
    }
  }
};

//addbyxqx
class ExprShlVisitor : public ExprVisitor {
private:
	ref<Expr> src, dst;
	
public:
	std::map< ref<Expr>, ref<Expr> > shlExprMap;  //collect the shl expr

public:
	ExprShlVisitor() {}
	ExprShlVisitor(ref<Expr> _src, ref<Expr> _dst) : src(_src), dst(_dst) {}

	Action visitExpr(const Expr &e) {
		if (e == *src.get()) {
			return Action::changeTo(dst);
		} else {
			return Action::doChildren();
		}
	}

	Action visitExprPost(const Expr &e) {
		if (e == *src.get()) {
			return Action::changeTo(dst);
		} else {
			return Action::doChildren();
		}
	}
	
	void showShlMap(){
		std::map< ref<Expr>, ref<Expr> >::const_iterator it = shlExprMap.begin();
		for(; it != shlExprMap.end(); ){
			it++;
		}
	}
	
	ref<Expr> ParseExpr(ref<Expr> e);
	
};



bool ConstraintManager::rewriteConstraints(ExprVisitor &visitor) {
  ConstraintManager::constraints_ty old;
  bool changed = false;

  constraints.swap(old);
  for (ConstraintManager::constraints_ty::iterator 
         it = old.begin(), ie = old.end(); it != ie; ++it) {
    ref<Expr> &ce = *it;
    ref<Expr> e = visitor.visit(ce);

    if (e!=ce) {
      addConstraintInternal(e); // enable further reductions
      changed = true;
    } else {
      constraints.push_back(ce);
    }
  }

  return changed;
}

void ConstraintManager::simplifyForValidConstraint(ref<Expr> e) {
  // XXX 
}

ref<Expr> ConstraintManager::simplifyExpr(ref<Expr> e) const {
  if (isa<ConstantExpr>(e))
    return e;

  std::map< ref<Expr>, ref<Expr> > equalities;
  
  for (ConstraintManager::constraints_ty::const_iterator 
         it = constraints.begin(), ie = constraints.end(); it != ie; ++it) {
    if (const EqExpr *ee = dyn_cast<EqExpr>(*it)) {
      if (isa<ConstantExpr>(ee->left)) {
        equalities.insert(std::make_pair(ee->right,
                                         ee->left));
      } else {
        equalities.insert(std::make_pair(*it,
                                         ConstantExpr::alloc(1, Expr::Bool)));
      }
    } else {
      equalities.insert(std::make_pair(*it,
                                       ConstantExpr::alloc(1, Expr::Bool)));
    }
  }

  return ExprReplaceVisitor2(equalities).visit(e);
}

void ConstraintManager::addConstraintInternal(ref<Expr> e) {
  // rewrite any known equalities 

  // XXX should profile the effects of this and the overhead.
  // traversing the constraints looking for equalities is hardly the
  // slowest thing we do, but it is probably nicer to have a
  // ConstraintSet ADT which efficiently remembers obvious patterns
  // (byte-constant comparison).

  switch (e->getKind()) {
  case Expr::Constant:
    assert(cast<ConstantExpr>(e)->isTrue() && 
           "attempt to add invalid (false) constraint");
    break;
    
    // split to enable finer grained independence and other optimizations
  case Expr::And: {
    BinaryExpr *be = cast<BinaryExpr>(e);
    addConstraintInternal(be->left);
    addConstraintInternal(be->right);
    break;
  }

  case Expr::Eq: {
    BinaryExpr *be = cast<BinaryExpr>(e);
    if (isa<ConstantExpr>(be->left)) {
      ExprReplaceVisitor visitor(be->right, be->left);
      rewriteConstraints(visitor);
    }
    constraints.push_back(e);
    break;
  }
    
  default:
    constraints.push_back(e);
    break;
  }
}

void ConstraintManager::addConstraint(ref<Expr> e) {
  e = simplifyExpr(e);
  addConstraintInternal(e);
}

//addbyxqx201303
Expr::Kind ConstraintManager::getConstraintExprKind(ref<Expr> e){
	return e->getKind();
}

//addbyxqx201303  
//parse a expr of constraint , find the mul or shl expr, and ops of them .
void ConstraintManager::ParseConstraintExpr(ref<Expr> e) {
	

	ExprShlVisitor visitor;
	
	visitor.visit(e);
	
	visitor.showShlMap();
	
	return ;
	
}

//addbyxqx201303
//parse a expr of constraint , find the mul or shl expr, and ops of them .
ref<Expr> ExprShlVisitor::ParseExpr(ref<Expr> e) {
	if (isa<ConstantExpr>(e))   
		return e;
	
	Expr &ep = *e.get();
	
	ExprVisitor::Action res =  visitExpr(ep);
	
	switch (ep.getKind()) {
		case Expr::NotOptimized: res = visitNotOptimized(static_cast<NotOptimizedExpr&>(ep)); break;
		case Expr::Read: res = visitRead(static_cast<ReadExpr&>(ep)); break;
		case Expr::Select: res = visitSelect(static_cast<SelectExpr&>(ep)); break;
		case Expr::Concat: res = visitConcat(static_cast<ConcatExpr&>(ep)); break;
		case Expr::Extract: res = visitExtract(static_cast<ExtractExpr&>(ep)); break;
		case Expr::ZExt: res = visitZExt(static_cast<ZExtExpr&>(ep)); break;
		case Expr::SExt: res = visitSExt(static_cast<SExtExpr&>(ep)); break;
		case Expr::Add: res = visitAdd(static_cast<AddExpr&>(ep)); break;
		case Expr::Sub: res = visitSub(static_cast<SubExpr&>(ep)); break;
		case Expr::Mul: res = visitMul(static_cast<MulExpr&>(ep)); break;
		case Expr::UDiv: res = visitUDiv(static_cast<UDivExpr&>(ep)); break;
		case Expr::SDiv: res = visitSDiv(static_cast<SDivExpr&>(ep)); break;
		case Expr::URem: res = visitURem(static_cast<URemExpr&>(ep)); break;
		case Expr::SRem: res = visitSRem(static_cast<SRemExpr&>(ep)); break;
		case Expr::Not: res = visitNot(static_cast<NotExpr&>(ep)); break;
		case Expr::And: res = visitAnd(static_cast<AndExpr&>(ep)); break;
		case Expr::Or: res = visitOr(static_cast<OrExpr&>(ep)); break;
		case Expr::Xor: res = visitXor(static_cast<XorExpr&>(ep)); break;
		case Expr::Shl: res = visitShl(static_cast<ShlExpr&>(ep)); break;
		case Expr::LShr: res = visitLShr(static_cast<LShrExpr&>(ep)); break;
		case Expr::AShr: res = visitAShr(static_cast<AShrExpr&>(ep)); break;
		case Expr::Eq: res = visitEq(static_cast<EqExpr&>(ep)); break;
		case Expr::Ne: res = visitNe(static_cast<NeExpr&>(ep)); break;
		case Expr::Ult: res = visitUlt(static_cast<UltExpr&>(ep)); break;
		case Expr::Ule: res = visitUle(static_cast<UleExpr&>(ep)); break;
		case Expr::Ugt: res = visitUgt(static_cast<UgtExpr&>(ep)); break;
		case Expr::Uge: res = visitUge(static_cast<UgeExpr&>(ep)); break;
		case Expr::Slt: res = visitSlt(static_cast<SltExpr&>(ep)); break;
		case Expr::Sle: res = visitSle(static_cast<SleExpr&>(ep)); break;
		case Expr::Sgt: res = visitSgt(static_cast<SgtExpr&>(ep)); break;
		case Expr::Sge: res = visitSge(static_cast<SgeExpr&>(ep)); break;
		case Expr::Constant:
		default:
			assert(0 && "invalid expression kind");
    }	

	if(ep.getKind() == Expr::Shl) {
		BinaryExpr *be = cast<BinaryExpr>(e);
		shlExprMap.insert(std::make_pair(be->right,
                                         be->left));	
	}
	
	switch(res.kind) {
		default:
			assert(0 && "invalid kind");
		case Action::DoChildren: {  
			//bool rebuild = false;
			ref<Expr> ee(&ep), kids[8];
			unsigned count = ep.getNumKids();
			for (unsigned i=0; i<count; i++) {
				ref<Expr> kid = ep.getKid(i);
				//std::cerr << "kids[" << i << "]: " << kid << "\n" ;
				//xgdb_printExpr(kid.get());
				kids[i] = ParseExpr(kid);
				
			}
			/*
			//what is meaning?
			if (!isa<ConstantExpr>(e)) {
			 res = visitExprPost(*e.get());
			 if (res.kind==Action::ChangeTo)
			  e = res.argument;
			}
			*/
			
			return e;
		}
		case Action::SkipChildren:
			return e;
		case Action::ChangeTo:
			return res.argument;
    }
	
	
	
}


/*
//addbyxqx201303  
//parse a expr of constraint , find the mul or shl expr, and ops of them .
bool ConstraintManager::ParseConstraintExpr(ref<Expr> e, ExprVisitor &visitor) {
	if (isa<ConstantExpr>(e))   
		return false;
		
	Expr &ep = *e.get();
	
	Action res ;
	
	switch (ep.getKind()) {
		case Expr::NotOptimized: res = visitor.visitNotOptimized(static_cast<NotOptimizedExpr&>(ep)); break;
		case Expr::Read: res = visitor.visitRead(static_cast<ReadExpr&>(ep)); break;
		case Expr::Select: res = visitor.visitSelect(static_cast<SelectExpr&>(ep)); break;
		case Expr::Concat: res = visitor.visitConcat(static_cast<ConcatExpr&>(ep)); break;
		case Expr::Extract: res = visitor.visitExtract(static_cast<ExtractExpr&>(ep)); break;
		case Expr::ZExt: res = visitor.visitZExt(static_cast<ZExtExpr&>(ep)); break;
		case Expr::SExt: res = visitor.visitSExt(static_cast<SExtExpr&>(ep)); break;
		case Expr::Add: res = visitor.visitAdd(static_cast<AddExpr&>(ep)); break;
		case Expr::Sub: res = visitor.visitSub(static_cast<SubExpr&>(ep)); break;
		case Expr::Mul: res = visitor.visitMul(static_cast<MulExpr&>(ep)); break;
		case Expr::UDiv: res = visitor.visitUDiv(static_cast<UDivExpr&>(ep)); break;
		case Expr::SDiv: res = visitor.visitSDiv(static_cast<SDivExpr&>(ep)); break;
		case Expr::URem: res = visitor.visitURem(static_cast<URemExpr&>(ep)); break;
		case Expr::SRem: res = visitor.visitSRem(static_cast<SRemExpr&>(ep)); break;
		case Expr::Not: res = visitor.visitNot(static_cast<NotExpr&>(ep)); break;
		case Expr::And: res = visitor.visitAnd(static_cast<AndExpr&>(ep)); break;
		case Expr::Or: res = visitor.visitOr(static_cast<OrExpr&>(ep)); break;
		case Expr::Xor: res = visitor.visitXor(static_cast<XorExpr&>(ep)); break;
		case Expr::Shl: res = visitor.visitShl(static_cast<ShlExpr&>(ep)); break;
		case Expr::LShr: res = visitor.visitLShr(static_cast<LShrExpr&>(ep)); break;
		case Expr::AShr: res = visitor.visitAShr(static_cast<AShrExpr&>(ep)); break;
		case Expr::Eq: res = visitor.visitEq(static_cast<EqExpr&>(ep)); break;
		case Expr::Ne: res = visitor.visitNe(static_cast<NeExpr&>(ep)); break;
		case Expr::Ult: res = visitor.visitUlt(static_cast<UltExpr&>(ep)); break;
		case Expr::Ule: res = visitor.visitUle(static_cast<UleExpr&>(ep)); break;
		case Expr::Ugt: res = visitor.visitUgt(static_cast<UgtExpr&>(ep)); break;
		case Expr::Uge: res = visitor.visitUge(static_cast<UgeExpr&>(ep)); break;
		case Expr::Slt: res = visitor.visitSlt(static_cast<SltExpr&>(ep)); break;
		case Expr::Sle: res = visitor.visitSle(static_cast<SleExpr&>(ep)); break;
		case Expr::Sgt: res = visitor.visitSgt(static_cast<SgtExpr&>(ep)); break;
		case Expr::Sge: res = visitor.visitSge(static_cast<SgeExpr&>(ep)); break;
		case Expr::Constant:
		default:
			assert(0 && "invalid expression kind");
    }	

		
	switch(res.kind) {
		default:
			assert(0 && "invalid kind");
		case Action::DoChildren: {  
			bool rebuild = false;
			ref<Expr> e(&ep), kids[8];
			unsigned count = ep.getNumKids();
			for (unsigned i=0; i<count; i++) {
				ref<Expr> kid = ep.getKid(i);
				//std::cerr << "kids[" << i << "]: " << kid << "\n" ;
				//xgdb_printExpr(kid.get());
				kids[i] = visit(kid);
				if (kids[i] != kid)
					rebuild = true;
			}
			if (rebuild) {
				e = ep.rebuild(kids);
				if (recursive)
					e = visit(e);
			}
			if (!isa<ConstantExpr>(e)) {
				res = visitExprPost(*e.get());
				if (res.kind==Action::ChangeTo)
					e = res.argument;
			}
			return e;
		}
		case Action::SkipChildren:
			return e;
		case Action::ChangeTo:
			return res.argument;
    }
			
			
	
}
*/

