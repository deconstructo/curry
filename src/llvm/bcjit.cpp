/*
 * Bytecode → LLVM IR translator.
 *
 * One Chunk becomes one native function
 *
 *     val_t fn(int32_t argc, val_t *args, BcClosure *self)
 *
 * that does what vm_run does for one CallFrame of that chunk. Because it
 * starts from the bytecode the VM already runs, it has no view of its own
 * on what a Scheme form means: macros are expanded, locals, upvalues and
 * globals are resolved, tail calls are marked. See
 * docs/thoughts/jit-bytecode-translator-plan-2026-11-15.md.
 *
 * Phase 1 (this file as it stands): correctness and coverage, not speed.
 *
 *   - Every value lives where the interpreter keeps it: stack position k
 *     of the frame is slots[k] on the VM stack. The stack height at each
 *     instruction is computed here (pass 2) and must be the same on every
 *     path that reaches it; a chunk where it is not is refused.
 *   - Everything that is not a load, a store or a branch is a call to a
 *     helper in vm.c (bcjit_rt.h), which runs the interpreter's own code.
 *   - vm->sp is written before every helper call, so the collector, an
 *     exception unwind and a nested call all see the frame as the
 *     interpreter would have left it.
 *   - A tail call to the function itself is a jump to its first
 *     instruction. Any other tail call is an ordinary call followed by a
 *     return; the call sites' depth limit (JIT_CALL_DEPTH_LIMIT) keeps
 *     the native stack bounded by sending deep chains back to bytecode.
 *
 * A chunk that uses an opcode not handled here is refused (the closure
 * stays on bytecode) with a message naming the opcode.
 */

#include "bcjit.h"

extern "C" {
#include "../bcjit_rt.h"
#include "../chunk.h"
#include "../opcode.h"
#include "../value.h"
#include "../object.h"
}

#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/raw_ostream.h>

#include <cstddef>
#include <stdexcept>
#include <string>
#include <vector>

using namespace llvm;

namespace curry {

namespace {

[[noreturn]] void refuse(const std::string &why) {
    throw std::runtime_error("JIT: " + why);
}

/* One decoded instruction. */
struct Insn {
    int     pc;        /* offset of the opcode byte               */
    int     next;      /* offset of the following instruction     */
    OpCode  op;
    int     a = 0;     /* first operand                           */
    int     b = 0;     /* second operand (OP_CALL_GLOBAL family)  */
};

/* What an instruction does to the stack height, and where control goes. */
struct Effect {
    int  pops;         /* values read from the top of the stack   */
    int  pushes;       /* values left in their place              */
    bool falls;        /* control can reach the next instruction  */
    bool jumps;        /* control can reach `a` (a jump target)   */
};

const char *op_name(OpCode op) {
    return ((int)op >= 0 && op < OP_COUNT) ? opcode_name[op] : "???";
}

/* ── Pass 1: decode ─────────────────────────────────────────────────── */

std::vector<Insn> decode(const Chunk *ch) {
    std::vector<Insn> insns;
    const uint8_t *code = ch->code;
    int len = ch->code_len;
    if (!code || len <= 0) refuse("empty chunk");

    /* The n operand bytes starting at `start` must be inside the code. */
    auto need = [&](int start, int n) {
        if (start + n > len) refuse("instruction runs past the end of the code");
    };
    auto check_const = [&](int ci, bool symbol) {
        if (ci < 0 || ci >= ch->const_len) refuse("constant index out of range");
        if (symbol && !vis_symbol(ch->constants[ci]))
            refuse("global-variable operand is not a symbol");
    };

    for (int pc = 0; pc < len; ) {
        Insn in;
        in.pc = pc;
        uint8_t raw = code[pc];
        if (raw >= OP_COUNT) refuse("unknown opcode byte");
        in.op = (OpCode)raw;
        int operands = 0;
        switch (in.op) {
        case OP_TRUE: case OP_FALSE: case OP_NIL: case OP_VOID:
        case OP_POP: case OP_DUP: case OP_SWAP: case OP_NOP:
        case OP_RETURN:
            break;

        case OP_CONST:
            operands = 1; need(pc + 1, 1);
            in.a = code[pc + 1];
            check_const(in.a, false);
            break;
        case OP_CONST_W:
            operands = 2; need(pc + 1, 2);
            in.a = code[pc + 1] | (code[pc + 2] << 8);
            check_const(in.a, false);
            break;

        case OP_LOAD_GLOBAL: case OP_STORE_GLOBAL: case OP_DEF_GLOBAL:
        case OP_ADD: case OP_SUB: case OP_MUL: case OP_NUMEQ:
        case OP_LT: case OP_LE: case OP_GT: case OP_GE:
        case OP_CONS: case OP_CAR: case OP_CDR: case OP_NULLP: case OP_PAIRP:
            operands = 1; need(pc + 1, 1);
            in.a = code[pc + 1];
            check_const(in.a, true);
            break;

        case OP_LOAD_LOCAL: case OP_STORE_LOCAL:
        case OP_LOAD_UP: case OP_STORE_UP:
        case OP_SLIDE: case OP_CLOSE_UP:
        case OP_CALL: case OP_TAIL_CALL: case OP_SELF_TAIL_CALL:
            operands = 1; need(pc + 1, 1);
            in.a = code[pc + 1];
            break;

        case OP_JUMP: case OP_JUMP_FALSE: case OP_JUMP_TRUE:
            operands = 2; need(pc + 1, 2);
            in.a = code[pc + 1] | (code[pc + 2] << 8);
            break;

        case OP_CALL_GLOBAL: case OP_TAIL_CALL_GLOBAL:
            operands = 2; need(pc + 1, 2);
            in.a = code[pc + 1];
            in.b = code[pc + 2];
            check_const(in.a, true);
            break;

        case OP_CLOSURE: {
            need(pc + 1, 1);
            in.a = code[pc + 1];
            check_const(in.a, false);
            val_t c = ch->constants[in.a];
            if (!vis_type(c, T_CHUNK)) refuse("OP_CLOSURE constant is not a chunk");
            int n = vunptr(Chunk, c)->upval_count;
            if (n < 0 || n > 255) refuse("OP_CLOSURE upvalue count out of range");
            operands = 1 + 2 * n;
            need(pc + 1, operands);
            in.b = n;
            break;
        }

        default:
            refuse(std::string("opcode ") + op_name(in.op) + " is not supported yet");
        }
        in.next = pc + 1 + operands;
        insns.push_back(in);
        pc = in.next;
    }
    return insns;
}

Effect effect_of(const Insn &in) {
    switch (in.op) {
    case OP_CONST: case OP_CONST_W:
    case OP_TRUE: case OP_FALSE: case OP_NIL: case OP_VOID:
    case OP_LOAD_LOCAL: case OP_LOAD_GLOBAL: case OP_LOAD_UP:
    case OP_CLOSURE:
        return {0, 1, true, false};
    case OP_STORE_LOCAL: case OP_STORE_GLOBAL: case OP_DEF_GLOBAL:
    case OP_STORE_UP: case OP_POP:
        return {1, 0, true, false};
    case OP_DUP:   return {1, 2, true, false};
    case OP_SWAP:  return {2, 2, true, false};
    case OP_NOP: case OP_CLOSE_UP:
        return {0, 0, true, false};
    case OP_SLIDE: return {in.a + 1, 1, true, false};
    case OP_ADD: case OP_SUB: case OP_MUL: case OP_NUMEQ:
    case OP_LT: case OP_LE: case OP_GT: case OP_GE: case OP_CONS:
        return {2, 1, true, false};
    case OP_CAR: case OP_CDR: case OP_NULLP: case OP_PAIRP:
        return {1, 1, true, false};
    case OP_JUMP:       return {0, 0, false, true};
    case OP_JUMP_FALSE: case OP_JUMP_TRUE:
        return {1, 0, true, true};
    case OP_CALL:        return {in.a + 1, 1, true, false};
    case OP_CALL_GLOBAL: return {in.b, 1, true, false};
    case OP_TAIL_CALL:        return {in.a + 1, 0, false, false};
    case OP_TAIL_CALL_GLOBAL: return {in.b, 0, false, false};
    case OP_SELF_TAIL_CALL:   return {in.a, 0, false, false};
    case OP_RETURN:           return {1, 0, false, false};
    default:
        refuse(std::string("opcode ") + op_name(in.op) + " is not supported yet");
    }
}

/* ── Pass 2: stack height at every reachable instruction ────────────── */

struct Layout {
    std::vector<Insn> insns;
    std::vector<int>  index_of_pc;   /* pc → index into insns, or -1     */
    std::vector<int>  height;        /* per insn; -1 = not reachable     */
    std::vector<bool> leader;        /* per insn: starts a basic block   */
    int entry_height = 0;
    int max_height   = 0;
};

Layout analyse(const Chunk *ch) {
    Layout L;
    L.insns = decode(ch);
    L.index_of_pc.assign(ch->code_len + 1, -1);
    for (size_t i = 0; i < L.insns.size(); i++) L.index_of_pc[L.insns[i].pc] = (int)i;
    L.height.assign(L.insns.size(), -1);
    L.leader.assign(L.insns.size(), false);

    /* The frame starts as the bound arguments: `arity` of them, or the
     * fixed ones plus one rest list (vm_bind_args). */
    L.entry_height = ch->arity >= 0 ? ch->arity : (-ch->arity - 1) + 1;
    L.max_height   = L.entry_height;

    std::vector<int> work;
    auto reach = [&](int idx, int h, bool is_leader) {
        if (is_leader) L.leader[idx] = true;
        if (L.height[idx] == -1) { L.height[idx] = h; work.push_back(idx); }
        else if (L.height[idx] != h)
            refuse("stack height differs between paths at offset " +
                   std::to_string(L.insns[idx].pc));
    };
    auto index_at = [&](int pc) {
        int idx = (pc >= 0 && pc < ch->code_len) ? L.index_of_pc[pc] : -1;
        if (idx < 0) refuse("jump into the middle of an instruction");
        return idx;
    };

    reach(0, L.entry_height, true);
    while (!work.empty()) {
        int idx = work.back(); work.pop_back();
        const Insn &in = L.insns[idx];
        int h = L.height[idx];
        Effect e = effect_of(in);
        if (h < e.pops) refuse("stack underflow at offset " + std::to_string(in.pc));
        /* A local is a stack position below the operands being used. */
        if ((in.op == OP_LOAD_LOCAL || in.op == OP_STORE_LOCAL || in.op == OP_CLOSE_UP) &&
            in.a >= h)
            refuse("local slot out of range at offset " + std::to_string(in.pc));
        if (in.op == OP_LOAD_UP || in.op == OP_STORE_UP) {
            if (in.a >= ch->upval_count)
                refuse("upvalue index out of range at offset " + std::to_string(in.pc));
        }
        if (in.op == OP_CLOSURE) {
            for (int k = 0; k < in.b; k++) {
                int is_local = ch->code[in.pc + 2 + 2 * k];
                int cidx     = ch->code[in.pc + 3 + 2 * k];
                if (is_local ? cidx >= h : cidx >= ch->upval_count)
                    refuse("captured variable out of range at offset " + std::to_string(in.pc));
            }
        }
        int after = h - e.pops + e.pushes;
        if (after > L.max_height) L.max_height = after;
        if (e.falls) {
            if (in.next >= ch->code_len) refuse("code runs off the end of the chunk");
            /* The instruction after a conditional jump starts a block. */
            reach(index_at(in.next), after, e.jumps);
        }
        if (e.jumps) reach(index_at(in.a), after, true);
    }
    return L;
}

/* ── Pass 3: emit ───────────────────────────────────────────────────── */

struct Emitter {
    LLVMContext  &ctx;
    llvm::Module &M;
    IRBuilder<>   B;
    const Chunk  *ch;
    const Layout &L;

    Type *i64, *i32, *ptr, *voidt;
    Function *fn = nullptr;
    Value *self = nullptr, *slots = nullptr, *entry_sp = nullptr, *sp_addr = nullptr,
          *ip_addr = nullptr;
    /* vm->sp is known to be at least slots + sp_h. The collector scans the
     * VM stack only up to sp + VM_GC_SLACK (16) while native code pushes
     * inline without moving sp, so sp is never allowed to lag the current
     * height by more than MAX_SP_LAG: see sync_sp_if_lagging. */
    int sp_h = 0;
    static constexpr int MAX_SP_LAG = 8;
    std::vector<BasicBlock *> block;   /* per insn; set for leaders */

    Emitter(LLVMContext &c, llvm::Module &m, const Chunk *chunk, const Layout &layout)
        : ctx(c), M(m), B(c), ch(chunk), L(layout),
          i64(Type::getInt64Ty(c)), i32(Type::getInt32Ty(c)),
          ptr(PointerType::getUnqual(c)), voidt(Type::getVoidTy(c)) {}

    Value *c64(uint64_t v) { return ConstantInt::get(i64, v); }
    Value *c32(int v)      { return ConstantInt::get(i32, (uint64_t)(int64_t)v, true); }
    /* An address known now and fixed for as long as the code can run: the
     * chunk is kept alive by every closure that can reach this code. */
    Value *addr(const void *p) { return B.CreateIntToPtr(c64((uint64_t)(uintptr_t)p), ptr); }
    Value *chunk_ptr() { return addr(ch); }

    Value *slot(int k) { return B.CreateConstInBoundsGEP1_64(i64, slots, (uint64_t)k); }
    Value *get(int k)  { return B.CreateLoad(i64, slot(k)); }
    void   put(int k, Value *v) { B.CreateStore(v, slot(k)); }
    /* vm->sp = slots + h: everything below h is live. */
    void   set_sp(int h) { B.CreateStore(slot(h), sp_addr); sp_h = h; }
    /* After an instruction that left height h: bring sp up if it lags. */
    void   sync_sp_if_lagging(int h) { if (h - sp_h > MAX_SP_LAG) set_sp(h); }
    /* Record the offset of the next instruction in the CallFrame, so a
     * backtrace taken inside a helper shows this instruction's line. */
    void   mark_ip(const Insn &in) {
        B.CreateStore(addr(ch->code + in.next), ip_addr);
    }
    static bool pure(OpCode op) {
        switch (op) {
        case OP_CONST: case OP_CONST_W: case OP_TRUE: case OP_FALSE: case OP_NIL:
        case OP_VOID: case OP_LOAD_LOCAL: case OP_STORE_LOCAL: case OP_LOAD_UP:
        case OP_POP: case OP_DUP: case OP_SWAP: case OP_NOP: case OP_SLIDE:
        case OP_JUMP: case OP_JUMP_FALSE: case OP_JUMP_TRUE:
        case OP_STORE_UP: case OP_CLOSE_UP: case OP_RETURN:
            return true;
        default:
            return false;
        }
    }

    FunctionCallee helper(const char *name, Type *ret, std::initializer_list<Type *> args) {
        return M.getOrInsertFunction(name, FunctionType::get(ret, args, false));
    }
    Value *call(const char *name, Type *ret, std::initializer_list<Type *> types,
                std::initializer_list<Value *> args) {
        return B.CreateCall(helper(name, ret, types), args);
    }

    void leave_and_return(Value *result) {
        call("bcjit_leave", voidt, {ptr, ptr}, {slots, entry_sp});
        B.CreateRet(result);
    }

    /* Tail call to this same closure: rebind the arguments and start
     * again from the first instruction. */
    void self_tail(int h, int argc) {
        set_sp(h);
        call("bcjit_self_tail", voidt, {ptr, ptr, i32, ptr},
             {self, slots, c32(argc), slot(h - argc)});
        B.CreateBr(block[0]);
    }

    /* A tail call to `callee` with the argc values on top of the stack.
     * If the callee is this closure, loop; otherwise call and return. */
    void tail_call(int h, int argc, Value *callee) {
        auto *self_bb  = BasicBlock::Create(ctx, "tail_self", fn);
        auto *other_bb = BasicBlock::Create(ctx, "tail_other", fn);
        Value *is_self = B.CreateICmpEQ(callee, B.CreatePtrToInt(self, i64));
        B.CreateCondBr(is_self, self_bb, other_bb);

        B.SetInsertPoint(self_bb);
        self_tail(h, argc);

        B.SetInsertPoint(other_bb);
        Value *r = call("bcjit_call", i64, {i64, i32, ptr},
                        {callee, c32(argc), slot(h - argc)});
        leave_and_return(r);
    }

    void emit_insn(const Insn &in, int h) {
        if (!pure(in.op) || (in.op == OP_JUMP && in.a <= in.pc) ||
            ((in.op == OP_JUMP_FALSE || in.op == OP_JUMP_TRUE) && in.a <= in.pc))
            mark_ip(in);   /* helpers, and the interrupt poll on a back edge */
        switch (in.op) {
        case OP_CONST: case OP_CONST_W:
            /* The constant pool is fixed once the chunk is compiled and
             * the collector does not move objects, so the value itself
             * can be written into the code. The chunk keeps it alive. */
            put(h, c64((uint64_t)ch->constants[in.a]));
            break;
        case OP_TRUE:  put(h, c64(V_TRUE));  break;
        case OP_FALSE: put(h, c64(V_FALSE)); break;
        case OP_NIL:   put(h, c64(V_NIL));   break;
        case OP_VOID:  put(h, c64(V_VOID));  break;

        case OP_LOAD_LOCAL:  put(h, get(in.a));     break;
        case OP_STORE_LOCAL: put(in.a, get(h - 1)); break;

        case OP_LOAD_GLOBAL:
            set_sp(h);
            put(h, call("bcjit_load_global", i64, {ptr, i32}, {chunk_ptr(), c32(in.a)}));
            break;
        case OP_STORE_GLOBAL:
            set_sp(h);
            call("bcjit_store_global", voidt, {ptr, i32, i64},
                 {chunk_ptr(), c32(in.a), get(h - 1)});
            break;
        case OP_DEF_GLOBAL:
            set_sp(h);
            call("bcjit_def_global", voidt, {ptr, i32, i64},
                 {chunk_ptr(), c32(in.a), get(h - 1)});
            break;

        case OP_LOAD_UP: {
            /* *self->upvals[a]->location */
            Value *up_addr = B.CreateConstInBoundsGEP1_64(
                Type::getInt8Ty(ctx), self,
                offsetof(BcClosure, upvals) + (uint64_t)in.a * sizeof(Upvalue *));
            Value *up  = B.CreateLoad(ptr, up_addr);
            Value *loc = B.CreateLoad(ptr, B.CreateConstInBoundsGEP1_64(
                Type::getInt8Ty(ctx), up, offsetof(Upvalue, location)));
            put(h, B.CreateLoad(i64, loc));
            break;
        }
        case OP_STORE_UP:
            call("bcjit_store_up", voidt, {ptr, i32, i64}, {self, c32(in.a), get(h - 1)});
            break;

        case OP_POP: case OP_NOP: break;
        case OP_DUP: put(h, get(h - 1)); break;
        case OP_SWAP: {
            Value *top = get(h - 1), *below = get(h - 2);
            put(h - 1, below);
            put(h - 2, top);
            break;
        }
        case OP_SLIDE:
            put(h - 1 - in.a, get(h - 1));
            break;

        case OP_ADD: case OP_SUB: case OP_MUL: case OP_NUMEQ:
        case OP_LT: case OP_LE: case OP_GT: case OP_GE:
            set_sp(h);
            put(h - 2, call("bcjit_binary", i64, {ptr, i32, i64, i64, i32},
                            {chunk_ptr(), c32(in.a), get(h - 2), get(h - 1), c32((int)in.op)}));
            break;
        case OP_CONS:
            set_sp(h);
            put(h - 2, call("bcjit_cons", i64, {ptr, i32, i64, i64},
                            {chunk_ptr(), c32(in.a), get(h - 2), get(h - 1)}));
            break;
        case OP_CAR: case OP_CDR: case OP_NULLP: case OP_PAIRP:
            set_sp(h);
            put(h - 1, call("bcjit_unary", i64, {ptr, i32, i64, i32},
                            {chunk_ptr(), c32(in.a), get(h - 1), c32((int)in.op)}));
            break;

        case OP_JUMP:
            if (in.a <= in.pc) { set_sp(h); call("bcjit_poll", voidt, {}, {}); }
            B.CreateBr(block[L.index_of_pc[in.a]]);
            break;
        case OP_JUMP_FALSE: case OP_JUMP_TRUE: {
            Value *v = get(h - 1);
            if (in.a <= in.pc) { set_sp(h - 1); call("bcjit_poll", voidt, {}, {}); }
            Value *is_false = B.CreateICmpEQ(v, c64(V_FALSE));
            BasicBlock *target = block[L.index_of_pc[in.a]];
            BasicBlock *next   = block[L.index_of_pc[in.next]];
            if (in.op == OP_JUMP_FALSE) B.CreateCondBr(is_false, target, next);
            else                        B.CreateCondBr(is_false, next, target);
            break;
        }

        case OP_CALL: {
            /* [callee, arg0 .. argN-1] → [result] */
            set_sp(h);
            Value *r = call("bcjit_call", i64, {i64, i32, ptr},
                            {get(h - in.a - 1), c32(in.a), slot(h - in.a)});
            put(h - in.a - 1, r);
            break;
        }
        case OP_CALL_GLOBAL: {
            /* [arg0 .. argN-1] → [result]; the callee is looked up after
             * the arguments were evaluated, as in the interpreter. */
            set_sp(h);
            Value *callee = call("bcjit_load_global", i64, {ptr, i32}, {chunk_ptr(), c32(in.a)});
            Value *r = call("bcjit_call", i64, {i64, i32, ptr},
                            {callee, c32(in.b), slot(h - in.b)});
            put(h - in.b, r);
            break;
        }
        case OP_TAIL_CALL:
            set_sp(h);
            tail_call(h, in.a, get(h - in.a - 1));
            break;
        case OP_TAIL_CALL_GLOBAL: {
            set_sp(h);
            Value *callee = call("bcjit_load_global", i64, {ptr, i32}, {chunk_ptr(), c32(in.a)});
            tail_call(h, in.b, callee);
            break;
        }
        case OP_SELF_TAIL_CALL:
            self_tail(h, in.a);
            break;
        case OP_RETURN:
            leave_and_return(get(h - 1));
            break;

        case OP_CLOSURE:
            set_sp(h);
            put(h, call("bcjit_closure", i64, {ptr, ptr, i32}, {self, slots, c32(in.pc)}));
            break;
        case OP_CLOSE_UP:
            call("bcjit_close_up", voidt, {ptr}, {slot(in.a)});
            break;

        default:
            refuse(std::string("opcode ") + op_name(in.op) + " is not supported yet");
        }
    }

    void run(const std::string &name) {
        auto *ft = FunctionType::get(i64, {i32, ptr, ptr}, false);
        fn = Function::Create(ft, Function::ExternalLinkage, name, M);
        fn->addFnAttr(Attribute::NoUnwind);
        Value *argc = fn->getArg(0);
        Value *args = fn->getArg(1);
        self        = fn->getArg(2);

        auto *entry = BasicBlock::Create(ctx, "entry", fn);
        block.assign(L.insns.size(), nullptr);
        for (size_t i = 0; i < L.insns.size(); i++)
            if (L.height[i] >= 0 && L.leader[i])
                block[i] = BasicBlock::Create(ctx, "pc" + std::to_string(L.insns[i].pc), fn);

        B.SetInsertPoint(entry);
        auto *frame_ty = ArrayType::get(ptr, 3);          /* BcJitFrame */
        static_assert(sizeof(BcJitFrame) == 3 * sizeof(void *), "BcJitFrame layout");
        static_assert(offsetof(BcJitFrame, sp_addr) == sizeof(void *), "BcJitFrame layout");
        static_assert(offsetof(BcJitFrame, ip_addr) == 2 * sizeof(void *), "BcJitFrame layout");
        Value *frame = B.CreateAlloca(frame_ty, nullptr, "frame");
        slots = call("bcjit_enter", ptr, {ptr, i32, ptr, ptr, i32},
                     {self, argc, args, frame, c32(L.max_height)});
        entry_sp = B.CreateLoad(ptr, B.CreateConstInBoundsGEP2_32(frame_ty, frame, 0, 0), "entry_sp");
        sp_addr  = B.CreateLoad(ptr, B.CreateConstInBoundsGEP2_32(frame_ty, frame, 0, 1), "sp_addr");
        ip_addr  = B.CreateLoad(ptr, B.CreateConstInBoundsGEP2_32(frame_ty, frame, 0, 2), "ip_addr");
        sp_h     = L.entry_height;   /* bcjit_enter leaves sp there */
        B.CreateBr(block[0]);

        bool open = false;   /* is the current block still without a terminator? */
        for (size_t i = 0; i < L.insns.size(); i++) {
            if (L.height[i] < 0) continue;               /* not reachable */
            if (block[i]) {
                if (open) B.CreateBr(block[i]);
                B.SetInsertPoint(block[i]);
                /* Predecessors keep sp within MAX_SP_LAG of the height. */
                sp_h = L.height[i] > MAX_SP_LAG ? L.height[i] - MAX_SP_LAG : 0;
            } else if (!open) {
                /* Reachable only by falling through, yet the instruction
                 * before it does not fall through: pass 2 would not have
                 * marked it reachable. */
                refuse("internal error: instruction without a block");
            }
            emit_insn(L.insns[i], L.height[i]);
            Effect e = effect_of(L.insns[i]);
            if (e.falls && !e.jumps)
                sync_sp_if_lagging(L.height[i] - e.pops + e.pushes);
            /* Conditional jumps end the block too (both edges emitted). */
            open = e.falls && !e.jumps;
        }
        if (open) refuse("code runs off the end of the chunk");
    }
};

} /* namespace */

std::unique_ptr<llvm::Module> bcjit_translate(LLVMContext &ctx, const Chunk *ch,
                                              const std::string &name) {
    /* Parameters are addressed by a one-byte slot index. */
    if (ch->upval_count < 0 || ch->arity > 255 || ch->arity < -256)
        refuse("malformed chunk");
    Layout L = analyse(ch);

    auto M = std::make_unique<llvm::Module>(name, ctx);
    Emitter em(ctx, *M, ch, L);
    em.run(name);

    std::string err;
    raw_string_ostream es(err);
    if (verifyModule(*M, &es)) refuse("generated code failed verification: " + err);
    return M;
}

} /* namespace curry */
