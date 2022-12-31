#include <cassert>
#include <cstdarg>
#include <cstdint>
#include <vector>

#include "asmcode.h"
#include "cpudefs.h"
#include "cpu.h"
#include "emu.h"
#include "translate.h"
#include "mem.h"

#include <emscripten.h>

enum Reg : uint8_t {
	R0 = 0, R1, R2, R3, R4, R5, R6, R7, R8, R9, R10, R11, R12, SP, LR, PC
};

#define MAX_TRANSLATIONS 0x40000
struct translation translation_table[MAX_TRANSLATIONS];
static unsigned int next_translation_index = 0;

static std::vector<char> code_buffer;

__attribute__ ((format (printf, 1, 2)))
static void emit(const char *code, ...)
{
    // Reserve some space
    const size_t maxlen = 256;
    size_t prev_size = code_buffer.size();
    code_buffer.resize(code_buffer.size() + maxlen);

    // Print into the buffer
    va_list va;
    va_start(va, code);
    size_t len = vsnprintf(code_buffer.data() + prev_size, maxlen, code, va);
    va_end(va);

    // Make sure it was not truncated
    assert(len < maxlen);
    if(len >= maxlen)
        abort();

    code_buffer.resize(prev_size + len);
}

static void emit_reg(uint8_t r)
{
    emit("HEAP32[%lu]", uintptr_t(&arm.reg[r]) >> 2);
}

static void emit_set_reg(uint8_t r, uint32_t v)
{
    emit("HEAP32[%lu] = 0x%x;", uintptr_t(&arm.reg[r]) >> 2, v);
}

static void emit_leave(uint32_t new_pc)
{
    uint32_t *ptr = reinterpret_cast<uint32_t*>(try_ptr(new_pc));
    if(ptr && RAM_FLAGS(ptr) & RF_CODE_TRANSLATED) {
        // TODO: Check cycle_count_delta and cpu_events
        emit("return fb_translations[%d]();", RAM_FLAGS(ptr) >> RFS_TRANSLATION_INDEX);
    } else {
        emit_reg(PC); emit("= 0x%x;\nreturn;", new_pc);
    }
}

static bool local_cpsr_emitted = false;
static void emit_local_cpsr()
{
    if(local_cpsr_emitted)
        return;

    emit("let cpsr_n = HEAP8[%lu], cpsr_z = HEAP8[%lu],"
    "    cpsr_c = HEAP8[%lu], cpsr_v = HEAP8[%lu];\n",
         &arm.cpsr_n, &arm.cpsr_z,
         &arm.cpsr_c, &arm.cpsr_v);

    local_cpsr_emitted = true;
}

bool translate_init()
{
    EM_ASM(fb_translations = []);
    next_translation_index = 0;
    return true;
}

void translate_deinit()
{
}

void translate(uint32_t pc_start, uint32_t *insn_ptr_start)
{
	if(next_translation_index >= MAX_TRANSLATIONS)
	{
		warn("Out of translation slots!");
		return;
    }

    uint32_t pc = pc_start, *insn_ptr = insn_ptr_start;

    // Pointer to struct translation for this block
    translation *this_translation = &translation_table[next_translation_index];
    // We know this already. end_ptr will be set after the loop
    this_translation->start_ptr = insn_ptr_start;

	// Whether we can avoid the jump to translation_next at the end
	bool jumps_away = false;
	// Whether to stop translating code
	bool stop_here = false;

    local_cpsr_emitted = false;

    code_buffer.resize(0);
    emit("fb_translations[%d] = () => {\n", next_translation_index);

    size_t last_instruction_code_end = code_buffer.size();

	while(1)
	{
		// Translate further?
		if(stop_here
		   || RAM_FLAGS(insn_ptr) & DONT_TRANSLATE
		   || (pc ^ pc_start) & ~0x3ff)
			goto exit_translation;

        emit("// 0x%x\n", pc);

		Instruction i;
		i.raw = *insn_ptr;

		if(unlikely(i.cond == CC_NV))
		{
			if((i.raw & 0xFD70F000) == 0xF550F000)
				goto instruction_translated; // PLD
			else
				goto unimpl;
		}
		else if(unlikely(i.cond != CC_AL))
        {
            emit_local_cpsr();

            // Conditional instruction
            if(i.cond & 1)
                emit("if(!(");
            else
                emit("if((");

            switch(i.cond)
            {
            case CC_EQ: case CC_NE: emit("cpsr_z)){"); break;
            case CC_CS: case CC_CC: emit("cpsr_c)){"); break;
            case CC_MI: case CC_PL: emit("cpsr_n)){"); break;
            case CC_VS: case CC_VC: emit("cpsr_v)){"); break;
            case CC_HI: case CC_LS: emit("!cpsr_z && cpsr_c)){"); break;
            case CC_GE: case CC_LT: emit("cpsr_n == cpsr_v)){"); break;
            case CC_GT: case CC_LE: emit("!cpsr_z && cpsr_n == cpsr_v)){"); break;
            }
		}

		if((i.raw & 0xE000090) == 0x0000090)
		{
            goto unimpl;
		}
		else if((i.raw & 0xD900000) == 0x1000000)
		{
			if((i.raw & 0xFFFFFD0) == 0x12FFF10)
            {
				//B(L)X
				if(i.bx.rm == PC)
					goto unimpl;

                emit_reg(PC); emit(" = "); emit_reg(i.bx.rm); emit(";\n");

                if(i.bx.l)
                    emit_set_reg(LR, pc + 4);

                // Switch to thumb if necessary
                emit("if(HEAP32[%lu] & 1) { HEAP32[%lu] -= 1; HEAP32[%lu] |= 0x20; }\n"
                     "return;",
                     uintptr_t(&arm.reg[15]) >> 2, uintptr_t(&arm.reg[15]) >> 2, uintptr_t(&arm.cpsr_low28) >> 2);

                if(i.cond == CC_AL)
					stop_here = jumps_away = true;
			}
			else
				goto unimpl;
		}
		else if((i.raw & 0xC000000) == 0x0000000)
		{
            // Data processing

            // Using pc is not supported
            if(i.data_proc.rd == PC
               || (!i.data_proc.imm && i.data_proc.rm == PC))
                goto unimpl;

            // Shortcut for simple register mov (mov rd, rm)
            if((i.raw & 0xFFF0FF0) == 0x1a00000)
            {
                emit_reg(i.data_proc.rd); emit(" = "); emit_reg(i.data_proc.rm); emit(";");
                goto instruction_translated;
            }

            goto unimpl;
		}
		else if((i.raw & 0xFF000F0) == 0x7F000F0)
			goto unimpl; // undefined
		else if((i.raw & 0xC000000) == 0x4000000)
		{
			// Memory access: LDR, STRB, etc.
            goto unimpl;
		}
		else if((i.raw & 0xE000000) == 0x8000000)
		{
			// LDM/STM
            goto unimpl;
		}
		else if((i.raw & 0xE000000) == 0xA000000)
		{
			if(i.branch.l)
			{
				// Save return address in LR
                emit_reg(LR); emit("= 0x%x;", pc + 4);
			}

            if(i.cond == CC_AL)
				jumps_away = stop_here = true;

            uint32_t addr = pc + ((int32_t)(i.raw << 8) >> 6) + 8;
            emit_leave(addr);
		}
		else
			goto unimpl;

		instruction_translated:

        // Close if body for conditional instructions
        if(i.cond != CC_AL)
            emit("}");

        last_instruction_code_end = code_buffer.size();

        if(insn_ptr == insn_ptr_start)
            RAM_FLAGS(insn_ptr) |= (RF_CODE_TRANSLATED | next_translation_index << RFS_TRANSLATION_INDEX);

		++insn_ptr;
		pc += 4;
	}

    unimpl:
    // Throw away partial translation
    code_buffer.resize(last_instruction_code_end);
	RAM_FLAGS(insn_ptr) |= RF_CODE_NO_TRANSLATE;

	exit_translation:

    // No virtual instruction got translated, just drop everything
	if(insn_ptr == insn_ptr_start)
		return;

    if(!jumps_away)
        emit_leave(pc);

    this_translation->end_ptr = insn_ptr;

    emit("};");
    code_buffer.push_back(0); // Terminate string
    emscripten_run_script(code_buffer.data());

	next_translation_index += 1;
}

EM_JS(void, translation_enter_js, (int idx), {
    fb_translations[idx]();
});

static bool in_translation = false;
void translation_enter(void *ptr)
{
    auto idx = RAM_FLAGS(ptr) >> RFS_TRANSLATION_INDEX;
    cycle_count_delta += translation_table[idx].end_ptr - translation_table[idx].start_ptr;
    in_translation = true;
    translation_enter_js(idx);
    in_translation = false;
}

static void _invalidate_translation(int index)
{
    // TODO: Not technically correct, only the first instruction has the flags set.
    uint32_t *start = translation_table[index].start_ptr;
    uint32_t *end   = translation_table[index].end_ptr;
    for (; start < end; start++)
        RAM_FLAGS(start) &= ~(RF_CODE_TRANSLATED | (~0u << RFS_TRANSLATION_INDEX));
}

void flush_translations()
{
	for(unsigned int index = 0; index < next_translation_index; index++)
        _invalidate_translation(index);

    EM_ASM(fb_translations = []);
    next_translation_index = 0;
}

void invalidate_translation(int index)
{
    /* Due to emit_leave referencing other translations, we can't just
       invalidate a single translation. */
    #ifdef SUPPORT_LINUX
        (void) index;
        flush_translations();
    #else
        _invalidate_translation(index);
    #endif
}

void translate_fix_pc()
{
    assert(!in_translation);
}
