/*
 * S2E Selective Symbolic Execution Framework
 *
 * a dissassembler by xqx 2013-06  
 * for disassemble the i386 insn now
 *
 */

#ifndef S2E_PLUGINS_DISASSEMBLER_H
#define S2E_PLUGINS_DISASSEMBLER_H

#include <s2e/Plugin.h>
#include <s2e/Plugins/CorePlugin.h>
#include <s2e/S2EExecutionState.h>


#define		MAX_MNEMONIC_LEN		32
#define		MAX_OPERAND_LEN		64
#define		MAX_INSTRUCTION_LEN	128

#define  ALL_PRINT       1   // ӡָ
#define  BLOCK_PRINT     2   //ӡĿʼָ
#define  CALL_PRINT      3   //ֻӡcallָ


namespace s2e {
namespace plugins {

class Disassembler : public Plugin
{
    S2E_PLUGIN
public:
    Disassembler(S2E* s2e): Plugin(s2e) {}

    void initialize();
    //void slotTranslateBlockStart(ExecutionSignal*, S2EExecutionState *state, 
    //    TranslationBlock *tb, uint64_t pc);
    //void slotExecuteBlockStart(S2EExecutionState* state, uint64_t pc);
	

	
	typedef struct _INSTRUCTION
	{
		/* prefixes */
	
		char	RepeatPrefix;
		char	SegmentPrefix;
		char	OperandPrefix;
		char	AddressPrefix;
	
		/* opcode */
	
		unsigned int	Opcode;
	
		/* ModR/M */
	
		char	ModRM;
	
		/* SIB */
	
		char	SIB;
	
		/* Displacement */
	
		unsigned int	Displacement;
	
		/* Immediate */
	
		unsigned int	Immediate;
		
		/* Linear address of this instruction */
	
		unsigned int	LinearAddress;
		
		/* Flags */
	
		char dFlag, wFlag, sFlag;
	
	
	} INSTRUCTION, *PINSTRUCTION;

public:
	unsigned char *ParseModRM(unsigned char *Code, PINSTRUCTION Instruction, char *OperandRM);
	unsigned char *ParseImmediate(unsigned char *Code, PINSTRUCTION Instruction, char *OperandImmediate);
	unsigned char *ParseSIB(unsigned char *Code, PINSTRUCTION Instruction, char *SIBStr);
	unsigned char *ParseRegModRM(unsigned char *Code, PINSTRUCTION Instruction, char *Operand1, char *Operand2);
	unsigned char *Disassemble(unsigned int LinearAddress, unsigned char *Code, PINSTRUCTION Instruction, char *InstructionStr, int &bPrint);

	
private:
    bool m_traceBlockTranslation;
    bool m_traceBlockExecution;
};

} // namespace plugins
} // namespace s2e

#endif // S2E_PLUGINS_DISASSEMBLER_H
