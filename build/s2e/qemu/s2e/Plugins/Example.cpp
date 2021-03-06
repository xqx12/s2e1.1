/*
 * S2E Selective Symbolic Execution Framework
 *
 * Copyright (c) 2010, Dependable Systems Laboratory, EPFL
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the Dependable Systems Laboratory, EPFL nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE DEPENDABLE SYSTEMS LABORATORY, EPFL BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Currently maintained by:
 *    Vitaly Chipounov <vitaly.chipounov@epfl.ch>
 *    Volodymyr Kuznetsov <vova.kuznetsov@epfl.ch>
 *
 * All contributors are listed in S2E-AUTHORS file.
 *
 */

#include "Example.h"
#include <s2e/S2E.h>
#include <s2e/ConfigFile.h>
#include <s2e/Utils.h>
#include <s2e/S2EExecutor.h>

#include <iostream>

#include <s2e/Plugins/ExecutionTracers/TestCaseGenerator.h>



namespace s2e {
namespace plugins {

S2E_DEFINE_PLUGIN(Example, "Example S2E plugin", "",);

void Example::initialize()
{
/*    m_traceBlockTranslation = s2e()->getConfig()->getBool(
                        getConfigKey() + ".traceBlockTranslation");
    m_traceBlockExecution = s2e()->getConfig()->getBool(
                        getConfigKey() + ".traceBlockExecution");

    s2e()->getCorePlugin()->onTranslateBlockStart.connect(
            sigc::mem_fun(*this, &Example::slotTranslateBlockStart));
*/	
	s2e()->getCorePlugin()->onStateSwitch.connect(sigc::mem_fun(*this, &Example::onStateSwitch));
	
	s2e()->getCorePlugin()->onStateTerminate.connect(sigc::mem_fun(*this, &Example::onStateTerminate));

	//addbyxqx
	//s2e()->getMessagesStream() << "Example Test \n " ;
}

//addbyxqx201303
void Example::onStateSwitch(S2EExecutionState* currentState, S2EExecutionState* nextState)
{
	//xgdb_printState(currentState,false);
	//s2e()->getExecutor()->xgdb_printState(s2e()->getMessagesStream(), *currentState , false);
	s2e()->getMessagesStream() << "Example  onStateSwitch\n " ;
}

void Example::onStateTerminate(S2EExecutionState* state)
{
	//xgdb_printState(currentState,false);
	s2e()->getMessagesStream() << "state[" << state->getID() <<  "]: +++++++++Example  onStateTerminate start\n " ;
	
	//s2e()->getExecutor()->dumpX86State(s2e()->getMessagesStream());
	//state->dumpX86State(s2e()->getXqxlogsStream());
		
	//s2e()->getExecutor()->xgdb_printState(s2e()->getMessagesStream(), *state , false);
	s2e()->getExecutor()->xgdb_printState(s2e()->getXqxlogsStream(), *state , false);
	//s2e()->getXqxlogsStream() << state->getConstraintExprKind(state->con)  << std::endl;
	s2e()->getXqxlogsStream() << "==================before parse================\n";
	for (klee::ConstraintManager::const_iterator it = state->constraints.begin(),
			ie = state->constraints.end(); it != ie; ++it) {
		s2e()->getXqxlogsStream() << *it << std::endl ;
		//s2e()->getXqxlogsStream() << state->getConstraintExprKind(*it)  << std::endl;
		state->ParseConstraintExpr(*it);
	}
	s2e()->getXqxlogsStream() << "==================after parse================\n";
	s2e()->getExecutor()->xgdb_printState(s2e()->getXqxlogsStream(), *state , false);
	
	
	s2e::plugins::TestCaseGenerator *tc =
		dynamic_cast<s2e::plugins::TestCaseGenerator*>(s2e()->getPlugin("TestCaseGenerator"));
	if (tc) {
		tc->MyprocessTestCase(*state);
	}
	
	s2e()->getMessagesStream() << "+++++++++Example  onStateTerminate end\n " ;
	
}

void Example::onFork(S2EExecutionState *state,
							 const std::vector<S2EExecutionState*>& newStates,
							 const std::vector<klee::ref<klee::Expr> >& newConditions
							)
{
	s2e()->getMessagesStream() << "Example  onFork\n " ;
	
}


void Example::slotTranslateBlockStart(ExecutionSignal *signal, 
                                      S2EExecutionState *state,
                                      TranslationBlock *tb,
                                      uint64_t pc)
{
    if(m_traceBlockTranslation)
        std::cout << "Translating block at " << std::hex << pc << std::dec << std::endl;
    if(m_traceBlockExecution)
        signal->connect(sigc::mem_fun(*this, &Example::slotExecuteBlockStart));
}

void Example::slotExecuteBlockStart(S2EExecutionState *state, uint64_t pc)
{
    std::cout << "Executing block at " << std::hex << pc << std::dec << std::endl;
}

} // namespace plugins
} // namespace s2e
