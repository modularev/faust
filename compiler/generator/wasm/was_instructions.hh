/************************************************************************
 ************************************************************************
    FAUST compiler
    Copyright (C) 2003-2016 GRAME, Centre National de Creation Musicale
    ---------------------------------------------------------------------
    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 ************************************************************************
 ************************************************************************/

#ifndef _WAS_INST_H
#define _WAS_INST_H

#include <iomanip>

#include "binop.hh"
#include "text_instructions.hh"
#include "typing_instructions.hh"
#include "struct_manager.hh"

using namespace std;

#define offStrNum ((gGlobal->gFloatSize == 1) ? 2 : ((gGlobal->gFloatSize == 2) ? 3 : 0))

#define audioPtrSize int(pow(2, offStrNum))
#define audioBufferSize int(pow(2, offStrNum))
#define wasmBlockSize int(pow(2, 16))

/*
 wast/wasm module ABI:

 - in internal memory mode, a memory segment is allocated, otherwise it is given by the external runtime
 - DSP fields start at offset 0, then followed by audio buffers
 - JSON string is written at offset 0, to be copied and converted in a string 
 by the runtime (JS or else) before using the DSP itsef.
 
*/

inline int pow2limit(int x)
{
    int n = wasmBlockSize; // Minimum = 64 kB
    while (n < x) { n = 2 * n; }
    return n;
}

// DSP size + (inputs + outputs) * (audioPtrSize + max_buffer_size * audioBufferSize), json_len
inline int genMemSize(int struct_size, int channels, int json_len)
{
    return std::max((pow2limit(std::max(json_len, struct_size + channels * (audioPtrSize + (8192 * audioBufferSize)))) / wasmBlockSize), 1);
}

// Base class for textual 'wast' and binary 'wasm' visitors
struct WASInst {
    
    // Description of math functions
    struct MathFunDesc {
        
        enum Gen { kWAS,    // Implemented in wasm definition
                kExtMath,   // Implemented in JS Math context
                kInt32WAS,  // Manually implemented in wast/wasm backends
                kExtWAS };  // Manually implemented in JS
        
        MathFunDesc()
        {}
        
        MathFunDesc(Gen mode, const string& name, WasmOp op, Typed::VarType type, int args)
        :fMode(mode), fName(name), fWasmOp(op), fType(type), fArgs(args)
        {}
        
        MathFunDesc(Gen mode, const string& name, Typed::VarType type, int args)
        :fMode(mode), fName(name), fWasmOp(WasmOp::Dummy), fType(type), fArgs(args)
        {}
        
        Gen fMode;
        string fName;
        WasmOp fWasmOp;
        Typed::VarType fType;
        int fArgs;
    };

    TypingVisitor fTypingVisitor;
    map <string, int> fFunctionSymbolTable;
    map <string, MathFunDesc> fMathLibTable;    // Table : field_name, math description
    map <string, MemoryDesc> fFieldTable;       // Table : field_name, { offset, size, type }
    int fStructOffset;                          // Keep the offset in bytes of the structure
    int fSubContainerType;
    bool fFastMemory;                           // Is true, assume $dsp is always 0 to simplify and speed up dsp memory access code
    
    WASInst(bool fast_memory = false)
    {
        // Integer version
        fMathLibTable["abs"] = MathFunDesc(MathFunDesc::Gen::kExtMath, "abs", Typed::kInt32, 1);
        fMathLibTable["min_i"] = MathFunDesc(MathFunDesc::Gen::kInt32WAS, "min_i", Typed::kInt32, 2);
        fMathLibTable["max_i"] = MathFunDesc(MathFunDesc::Gen::kInt32WAS, "max_i", Typed::kInt32, 2);
        
        // Float version
        fMathLibTable["fabsf"] = MathFunDesc(MathFunDesc::Gen::kWAS, "abs", WasmOp::F32Abs, Typed::kFloat, 1);
        fMathLibTable["acosf"] = MathFunDesc(MathFunDesc::Gen::kExtMath, "acos", Typed::kFloat, 1);
        fMathLibTable["asinf"] = MathFunDesc(MathFunDesc::Gen::kExtMath, "asin", Typed::kFloat, 1);
        fMathLibTable["atanf"] = MathFunDesc(MathFunDesc::Gen::kExtMath, "atan", Typed::kFloat, 1);
        fMathLibTable["atan2f"] = MathFunDesc(MathFunDesc::Gen::kExtMath, "atan2", Typed::kFloat, 2);
        fMathLibTable["ceilf"] = MathFunDesc(MathFunDesc::Gen::kWAS, "ceil", WasmOp::F32Ceil, Typed::kFloat, 1);
        fMathLibTable["cosf"] = MathFunDesc(MathFunDesc::Gen::kExtMath, "cos", Typed::kFloat, 1);
        fMathLibTable["expf"] = MathFunDesc(MathFunDesc::Gen::kExtMath, "exp", Typed::kFloat, 1);
        fMathLibTable["floorf"] = MathFunDesc(MathFunDesc::Gen::kWAS, "floor", WasmOp::F32Floor, Typed::kFloat, 1);
        fMathLibTable["fmodf"] = MathFunDesc(MathFunDesc::Gen::kExtWAS, "fmod", Typed::kFloat, 2);
        fMathLibTable["logf"] = MathFunDesc(MathFunDesc::Gen::kExtMath, "log", Typed::kFloat, 1);
        fMathLibTable["log10f"] = MathFunDesc(MathFunDesc::Gen::kExtWAS, "log10", Typed::kFloat, 1);
        fMathLibTable["max_f"] = MathFunDesc(MathFunDesc::Gen::kWAS, "max", WasmOp::F32Max, Typed::kFloat, 2);
        fMathLibTable["min_f"] = MathFunDesc(MathFunDesc::Gen::kWAS, "min", WasmOp::F32Min, Typed::kFloat, 2);
        fMathLibTable["powf"] = MathFunDesc(MathFunDesc::Gen::kExtMath, "pow", Typed::kFloat, 2);
        fMathLibTable["remainderf"] = MathFunDesc(MathFunDesc::Gen::kExtWAS, "remainder", Typed::kFloat, 2);
        fMathLibTable["roundf"] = MathFunDesc(MathFunDesc::Gen::kExtMath, "round", Typed::kFloat, 1);
        fMathLibTable["sinf"] = MathFunDesc(MathFunDesc::Gen::kExtMath, "sin", Typed::kFloat, 1);
        fMathLibTable["sqrtf"] = MathFunDesc(MathFunDesc::Gen::kWAS, "sqrt", WasmOp::F32Sqrt, Typed::kFloat, 1);
        fMathLibTable["tanf"] = MathFunDesc(MathFunDesc::Gen::kExtMath, "tan", Typed::kFloat, 1);
        
        // Double version
        fMathLibTable["fabs"] = MathFunDesc(MathFunDesc::Gen::kWAS, "abs", WasmOp::F64Abs, Typed::kDouble, 1);
        fMathLibTable["acos"] = MathFunDesc(MathFunDesc::Gen::kExtMath, "acos", Typed::kDouble, 1);
        fMathLibTable["asin"] = MathFunDesc(MathFunDesc::Gen::kExtMath, "asin", Typed::kDouble, 1);
        fMathLibTable["atan"] = MathFunDesc(MathFunDesc::Gen::kExtMath, "atan", Typed::kDouble, 1);
        fMathLibTable["atan2"] = MathFunDesc(MathFunDesc::Gen::kExtMath, "atan2", Typed::kDouble, 2);
        fMathLibTable["ceil"] = MathFunDesc(MathFunDesc::Gen::kWAS, "ceil", WasmOp::F64Ceil, Typed::kDouble, 1);
        fMathLibTable["cos"] = MathFunDesc(MathFunDesc::Gen::kExtMath, "cos", Typed::kDouble, 1);
        fMathLibTable["exp"] = MathFunDesc(MathFunDesc::Gen::kExtMath, "exp", Typed::kDouble, 1);
        fMathLibTable["floor"] = MathFunDesc(MathFunDesc::Gen::kWAS, "floor", WasmOp::F64Floor, Typed::kDouble, 1);
        fMathLibTable["fmod"] = MathFunDesc(MathFunDesc::Gen::kExtWAS, "fmod", Typed::kDouble, 2);
        fMathLibTable["log"] = MathFunDesc(MathFunDesc::Gen::kExtMath, "log", Typed::kDouble, 1);
        fMathLibTable["log10"] = MathFunDesc(MathFunDesc::Gen::kExtWAS, "log10", Typed::kDouble, 1);
        fMathLibTable["max_"] = MathFunDesc(MathFunDesc::Gen::kWAS, "max", WasmOp::F64Max, Typed::kDouble, 2);
        fMathLibTable["min_"] = MathFunDesc(MathFunDesc::Gen::kWAS, "min", WasmOp::F64Min, Typed::kDouble, 2);
        fMathLibTable["pow"] = MathFunDesc(MathFunDesc::Gen::kExtMath, "pow", Typed::kDouble, 2);
        fMathLibTable["remainder"] = MathFunDesc(MathFunDesc::Gen::kExtWAS, "remainder", Typed::kDouble, 2);
        fMathLibTable["round"] = MathFunDesc(MathFunDesc::Gen::kExtMath, "round", Typed::kDouble, 1);
        fMathLibTable["sin"] = MathFunDesc(MathFunDesc::Gen::kExtMath, "sin", Typed::kDouble, 1);
        fMathLibTable["sqrt"] = MathFunDesc(MathFunDesc::Gen::kWAS, "sqrt", WasmOp::F64Sqrt, Typed::kDouble, 1);
        fMathLibTable["tan"] = MathFunDesc(MathFunDesc::Gen::kExtMath, "tan", Typed::kDouble, 1);
        
        fStructOffset = 0;
        fSubContainerType = -1;
        fFastMemory = fast_memory;
    }
    
    void setSubContainerType(int type) { fSubContainerType = type; }
    int getSubContainerType() { return fSubContainerType; }
    
    // The DSP size in bytes
    int getStructSize() { return fStructOffset; }
    
    map <string, MemoryDesc>& getFieldTable() { return fFieldTable; }
    
    int getFieldOffset(const string& name)
    {
        return (fFieldTable.find(name) != fFieldTable.end()) ? fFieldTable[name].fOffset : -1;
    }
    
    static DeclareFunInst* generateIntMin();
    static DeclareFunInst* generateIntMax();
    
    static DeclareFunInst* generateInit();
    static DeclareFunInst* generateInstanceInit();
    
    static DeclareFunInst* generateGetSampleRate();
    
};

#endif