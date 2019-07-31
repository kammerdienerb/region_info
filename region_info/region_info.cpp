// region_info.cpp
// llvm pass
// Brandon Kammerdiener -- 2019

#include <llvm/Config/llvm-config.h>
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Operator.h"
#include <llvm/IR/IRBuilder.h>
#include "llvm/Pass.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_set>
#include <stdio.h>
#include <sys/fcntl.h>
#include <unistd.h>
#include <vector>
#include <utility>

using namespace llvm;

namespace {

struct region_info : public ModulePass {
    static char ID;

    Module                          *theModule;
    IRBuilder<>                     *builder;
    FILE                            *source_location_file;
    std::unordered_set<std::string>  allocFnSet;
    std::unordered_set<std::string>  callocFnSet;
    std::unordered_set<std::string>  reallocFnSet;
    std::unordered_set<std::string>  dallocFnSet;

    region_info() :
        ModulePass(ID),
        theModule(nullptr) {

	/* C */
        allocFnSet.insert("malloc");
        allocFnSet.insert("aligned_alloc");
        allocFnSet.insert("posix_memalign");
        allocFnSet.insert("memalign");
        callocFnSet.insert("calloc");
        reallocFnSet.insert("realloc");
        dallocFnSet.insert("free");

	/* C++ */
        allocFnSet.insert("_Znam");
        allocFnSet.insert("_Znwm");
        dallocFnSet.insert("_ZdaPv");
        dallocFnSet.insert("_ZdlPv");

	/* Fortran */
        allocFnSet.insert("f90_alloc");
        allocFnSet.insert("f90_alloca");
        allocFnSet.insert("f90_alloc03");
        allocFnSet.insert("f90_alloc03a");
        allocFnSet.insert("f90_alloc03_chk");
        allocFnSet.insert("f90_alloc03_chka");
        allocFnSet.insert("f90_alloc04");
        allocFnSet.insert("f90_alloc04a");
        allocFnSet.insert("f90_alloc04_chk");
        allocFnSet.insert("f90_alloc04_chka");
        allocFnSet.insert("f90_kalloc");
        allocFnSet.insert("f90_calloc");
        allocFnSet.insert("f90_ptr_alloc");
        allocFnSet.insert("f90_ptr_alloca");
        allocFnSet.insert("f90_ptr_alloc03");
        allocFnSet.insert("f90_ptr_alloc03a");
        allocFnSet.insert("f90_ptr_alloc04");
        allocFnSet.insert("f90_ptr_alloc04a");
        allocFnSet.insert("f90_ptr_src_alloc03");
        allocFnSet.insert("f90_ptr_src_alloc03a");
        allocFnSet.insert("f90_ptr_src_alloc04");
        allocFnSet.insert("f90_ptr_src_alloc04a");
        allocFnSet.insert("f90_ptr_kalloc");
        allocFnSet.insert("f90_auto_allocv");
        allocFnSet.insert("f90_auto_alloc");
        allocFnSet.insert("f90_auto_alloc04");
        allocFnSet.insert("f90_alloc_i8");
        allocFnSet.insert("f90_alloca_i8");
        allocFnSet.insert("f90_alloc03_i8");
        allocFnSet.insert("f90_alloc03a_i8");
        allocFnSet.insert("f90_alloc03_chk_i8");
        allocFnSet.insert("f90_alloc03_chka_i8");
        allocFnSet.insert("f90_alloc04_i8");
        allocFnSet.insert("f90_alloc04a_i8");
        allocFnSet.insert("f90_alloc04_chk_i8");
        allocFnSet.insert("f90_alloc04_chka_i8");
        allocFnSet.insert("f90_kalloc_i8");
        allocFnSet.insert("f90_ptr_alloc_i8");
        allocFnSet.insert("f90_ptr_alloca_i8");
        allocFnSet.insert("f90_ptr_alloc03_i8");
        allocFnSet.insert("f90_ptr_alloc03a_i8");
        allocFnSet.insert("f90_ptr_alloc04_i8");
        allocFnSet.insert("f90_ptr_alloc04a_i8");
        allocFnSet.insert("f90_ptr_src_alloc03_i8");
        allocFnSet.insert("f90_ptr_src_alloc03a_i8");
        allocFnSet.insert("f90_ptr_src_alloc04_i8");
        allocFnSet.insert("f90_ptr_src_alloc04a_i8");
        allocFnSet.insert("f90_ptr_kalloc_i8");
        allocFnSet.insert("f90_auto_allocv_i8");
        allocFnSet.insert("f90_auto_alloc_i8");
        allocFnSet.insert("f90_auto_alloc04_i8");
        callocFnSet.insert("f90_calloc03");
        callocFnSet.insert("f90_calloc03a");
        callocFnSet.insert("f90_calloc04");
        callocFnSet.insert("f90_calloc04a");
        callocFnSet.insert("f90_kcalloc");
        callocFnSet.insert("f90_ptr_src_calloc03");
        callocFnSet.insert("f90_ptr_src_calloc03a");
        callocFnSet.insert("f90_ptr_src_calloc04");
        callocFnSet.insert("f90_ptr_src_calloc04a");
        callocFnSet.insert("f90_ptr_calloc");
        callocFnSet.insert("f90_ptr_calloc03");
        callocFnSet.insert("f90_ptr_calloc03a");
        callocFnSet.insert("f90_ptr_calloc04");
        callocFnSet.insert("f90_ptr_calloc04a");
        callocFnSet.insert("f90_ptr_kcalloc");
        callocFnSet.insert("f90_auto_calloc");
        callocFnSet.insert("f90_auto_calloc04");
        callocFnSet.insert("f90_calloc_i8");
        callocFnSet.insert("f90_calloc03_i8");
        callocFnSet.insert("f90_calloc03a_i8");
        callocFnSet.insert("f90_calloc04_i8");
        callocFnSet.insert("f90_calloc04a_i8");
        callocFnSet.insert("f90_kcalloc_i8");
        callocFnSet.insert("f90_ptr_src_calloc03_i8");
        callocFnSet.insert("f90_ptr_src_calloc03a_i8");
        callocFnSet.insert("f90_ptr_src_calloc04_i8");
        callocFnSet.insert("f90_ptr_src_calloc04a_i8");
        callocFnSet.insert("f90_ptr_calloc_i8");
        callocFnSet.insert("f90_ptr_calloc03_i8");
        callocFnSet.insert("f90_ptr_calloc03a_i8");
        callocFnSet.insert("f90_ptr_calloc04_i8");
        callocFnSet.insert("f90_ptr_calloc04a_i8");
        callocFnSet.insert("f90_ptr_kcalloc_i8");
        callocFnSet.insert("f90_auto_calloc_i8");
        callocFnSet.insert("f90_auto_calloc04_i8");
        dallocFnSet.insert("f90_dealloc");
        dallocFnSet.insert("f90_dealloca");
        dallocFnSet.insert("f90_dealloc03");
        dallocFnSet.insert("f90_dealloc03a");
        dallocFnSet.insert("f90_dealloc_mbr");
        dallocFnSet.insert("f90_dealloc_mbr03");
        dallocFnSet.insert("f90_dealloc_mbr03a");
        dallocFnSet.insert("f90_deallocx");
        dallocFnSet.insert("f90_auto_dealloc");
        dallocFnSet.insert("f90_dealloc_i8");
        dallocFnSet.insert("f90_dealloca_i8");
        dallocFnSet.insert("f90_dealloc03_i8");
        dallocFnSet.insert("f90_dealloc03a_i8");
        dallocFnSet.insert("f90_dealloc_mbr_i8");
        dallocFnSet.insert("f90_dealloc_mbr03_i8");
        dallocFnSet.insert("f90_dealloc_mbr03a_i8");
        dallocFnSet.insert("f90_deallocx_i8");
        dallocFnSet.insert("f90_auto_dealloc_i8");

        /* openMP */
        allocFnSet.insert("omp_alloc");
        allocFnSet.insert("omp_alloc_safe_align");
        dallocFnSet.insert("omp_free");
    }

    ///////////// Make the use of CallSite generic across CallInst and InvokeInst
    /////////////////////////////////////////////////////////////////////////////
    bool isCallSite(llvm::Instruction * inst) {
        return isa<CallInst>(inst) || isa<InvokeInst>(inst);
    }

    Function * getDirectlyCalledFunction(CallSite & site) {
        Function * fn  = nullptr;

        CallInst * call = dyn_cast<CallInst>(site.getInstruction());
        if (call) {
            fn = call->getCalledFunction();
        } else {
            InvokeInst * inv = dyn_cast<InvokeInst>(site.getInstruction());
            fn = inv->getCalledFunction();
        }

        return fn;
    }
    
    Value * getCalledValue(CallSite & site) {
        Value    * val = nullptr;

        CallInst * call = dyn_cast<CallInst>(site.getInstruction());
        if (call) {
            val = call->getCalledValue();
        } else {
            InvokeInst * inv = dyn_cast<InvokeInst>(site.getInstruction());
            val = inv->getCalledValue();
        }

        return val;
    }

    // Try to return the function that that a given call site calls.
    // If the call is direct this is easy.
    // If the call is indirect, we may still be able to get the function
    // if it is known statically.
    Function * getCalledFunction(CallSite & site) {
        Function * fn  = nullptr;
        Value    * val = nullptr;

        fn = getDirectlyCalledFunction(site);
        if (fn)
            return fn;

        // At this point, we are looking at an indirect call (common in code 
        // generated by flang).
        // We should still try to find the function if it is a simple bitcast:
        //
        //      %0 = bitcast void (...)* @f90_alloc04_chk to
        //              void (i8*, i8*, i8*, i8*, i8*, i8*, i8*, i8*, i8*, i64, ...)*
        //
        // So, we will try to scan through bitcasts.
        //
        // @incomplete: are there other operations that would make a statically direct
        // call look indirect?

        val = getCalledValue(site);

        if (BitCastOperator * op = dyn_cast<BitCastOperator>(val))
            return cast<Function>(op->stripPointerCasts());

        return nullptr;
    }

    User::op_iterator arg_begin(CallSite & site) {
        CallInst * call = dyn_cast<CallInst>(site.getInstruction());
        if (call)
            return call->arg_begin();
        return dyn_cast<InvokeInst>(site.getInstruction())->arg_begin();
    }

    User::op_iterator arg_end(CallSite & site) {
        CallInst * call = dyn_cast<CallInst>(site.getInstruction());
        if (call)
            return call->arg_end();
        return dyn_cast<InvokeInst>(site.getInstruction())->arg_end();
    }
    ////////////////////////////////////////////////////////////////////////////////


    ///////////////////////////////////////////////////////////// declareAllocInstFn
    ////////////////////////////////////////////////////////////////////////////////
    // Create the function bindings for the allocation instrumentation routine.
    ////////////////////////////////////////////////////////////////////////////////
    Function *declareAllocInstFn() {
        Type * pointer_type = Type::getInt8PtrTy(theModule->getContext());
        Type * int_type     = Type::getInt64Ty(theModule->getContext());

        std::vector<Type*> param_types { pointer_type, int_type, int_type };

        FunctionType * fn_type = FunctionType::get(Type::getVoidTy(theModule->getContext()), param_types, false);

        Function * fn = Function::Create(fn_type, GlobalValue::LinkageTypes::ExternalLinkage, "region_info_inst_alloc", theModule);

        return fn;
    }

    /////////////////////////////////////////////////////////// declareReallocInstFn
    ////////////////////////////////////////////////////////////////////////////////
    // Create the function bindings for the reallocation instrumentation routine.
    ////////////////////////////////////////////////////////////////////////////////
    Function *declareReallocInstFn() {
        Type * pointer_type = Type::getInt8PtrTy(theModule->getContext());
        Type * int_type     = Type::getInt64Ty(theModule->getContext());

        std::vector<Type*> param_types { pointer_type, pointer_type, int_type, int_type };

        FunctionType * fn_type = FunctionType::get(Type::getVoidTy(theModule->getContext()), param_types, false);

        Function * fn = Function::Create(fn_type, GlobalValue::LinkageTypes::ExternalLinkage, "region_info_inst_realloc", theModule);

        return fn;
    }
    ///////////////////////////////////////////////////////////// declareDeallocInstFn
   
    //////////////////////////////////////////////////////////////////////////////////
    // Create the function bindings for the deallocation instrumentation routine.
    //////////////////////////////////////////////////////////////////////////////////
    Function *declareDeallocInstFn() {
        Type * pointer_type = Type::getInt8PtrTy(theModule->getContext());
        Type * int_type     = Type::getInt32Ty(theModule->getContext());

        std::vector<Type*> param_types { pointer_type };

        FunctionType * fn_type = FunctionType::get(Type::getVoidTy(theModule->getContext()), param_types, false);

        Function * fn = Function::Create(fn_type, GlobalValue::LinkageTypes::ExternalLinkage, "region_info_inst_dealloc", theModule);

        return fn;
    }

    ////////////////////////////////////////////////////////////// declareReadInstFn
    ////////////////////////////////////////////////////////////////////////////////
    // Create the function bindings for the read instrumentation routine.
    ////////////////////////////////////////////////////////////////////////////////
    Function *declareReadInstFn() {
        Type * pointer_type = Type::getInt8PtrTy(theModule->getContext());

        std::vector<Type*> param_types { pointer_type };

        FunctionType * fn_type = FunctionType::get(Type::getVoidTy(theModule->getContext()), param_types, false);

        Function * fn = Function::Create(fn_type, GlobalValue::LinkageTypes::ExternalLinkage, "region_info_inst_read", theModule);

        return fn;
    }

    ////////////////////////////////////////////////////////////// declareReadInstFn
    ////////////////////////////////////////////////////////////////////////////////
    // Create the function bindings for the read instrumentation routine.
    ////////////////////////////////////////////////////////////////////////////////
    Function *declareWriteInstFn() {
        Type * pointer_type = Type::getInt8PtrTy(theModule->getContext());

        std::vector<Type*> param_types { pointer_type };

        FunctionType * fn_type = FunctionType::get(Type::getVoidTy(theModule->getContext()), param_types, false);

        Function * fn = Function::Create(fn_type, GlobalValue::LinkageTypes::ExternalLinkage, "region_info_inst_write", theModule);

        return fn;
    }

    /////////////////////////////////////////////////////// declareParallelEnterInstFn
    //////////////////////////////////////////////////////////////////////////////////
    // Create the function bindings for entering a parallel region.
    //////////////////////////////////////////////////////////////////////////////////
    Function *declareParallelEnterInstFn() {
        Type * int_type = Type::getInt64Ty(theModule->getContext());

        std::vector<Type*> param_types { int_type };

        FunctionType * fn_type = FunctionType::get(Type::getVoidTy(theModule->getContext()), param_types, false);

        Function * fn = Function::Create(fn_type, GlobalValue::LinkageTypes::ExternalLinkage, "region_info_inst_parallel_enter", theModule);

        return fn;
    }

    //////////////////////////////////////////////////////// declareParallelExitInstFn
    //////////////////////////////////////////////////////////////////////////////////
    // Create the function bindings for exiting a parallel region.
    //////////////////////////////////////////////////////////////////////////////////
    Function *declareParallelExitInstFn() {
        Type * int_type = Type::getInt64Ty(theModule->getContext());

        std::vector<Type*> param_types { int_type };

        FunctionType * fn_type = FunctionType::get(Type::getVoidTy(theModule->getContext()), param_types, false);

        Function * fn = Function::Create(fn_type, GlobalValue::LinkageTypes::ExternalLinkage, "region_info_inst_parallel_exit", theModule);

        return fn;
    }

    /////////////////////////////////////////////////////////// output_source_location
    //////////////////////////////////////////////////////////////////////////////////
    // Output a mapping from an allocation site ID to a source line.
    //////////////////////////////////////////////////////////////////////////////////
    void output_source_location(CallSite site, uint64_t id, int is_region) {
        auto & loc = site->getDebugLoc();

        auto parent_function_name = site->getParent()->getParent()->getName().str();

        const char *kind_str = "allocation site";
        if (is_region) { kind_str = "parallel region"; }

        if (loc) {
            const char  *inlined = "";
            std::string  file    = cast<DIScope>(loc.getScope())->getFilename().str();
            uint64_t     line    = loc.getLine();
            
            if (loc.getInlinedAt())    { inlined = " (inlined)"; }

            fprintf(source_location_file, "%s %llu: %s line %llu%s in %s\n", kind_str, id, file.c_str(), line, inlined, parent_function_name.c_str());
        } else {
            fprintf(source_location_file, "%s %llu: in %s (missing debug information)\n", kind_str, id, parent_function_name.c_str());
        }
    }
    
    //////////////////////////////////////////////////////////////////////// site_hash
    //////////////////////////////////////////////////////////////////////////////////
    // Give a unique ID to an allocation site.
    //////////////////////////////////////////////////////////////////////////////////
    uint64_t site_hash(uint64_t id) {
        const static uint64_t file_hash = std::hash<std::string>{}(theModule->getName().str());

        return file_hash + id;
    }

    ////////////////////////////////////////////////////////////////////// region_hash
    //////////////////////////////////////////////////////////////////////////////////
    // Give a unique ID to a parallel region.
    //////////////////////////////////////////////////////////////////////////////////
    uint64_t region_hash(uint64_t id) {
        const static uint64_t file_hash = std::hash<std::string>{}(theModule->getName().str());

        return file_hash + id;
    }

    ///////////////////////////////////////////////////////////// transformCallSites
    ////////////////////////////////////////////////////////////////////////////////
    // Adds instrumentation to allocation call sites.
    ////////////////////////////////////////////////////////////////////////////////
    void transformCallSites() {
        Type * pointer_type = Type::getInt8PtrTy(theModule->getContext());
        Type * int_type     = Type::getInt64Ty(theModule->getContext());

        std::vector<Instruction*> alloc_sites, calloc_sites, realloc_sites, dealloc_sites, forks;

        /* Collect all of the call sites. */
        for (auto& Fn : *theModule) {
            for (auto& BB : Fn) {
                for (auto inst = BB.begin(); inst != BB.end(); inst++) {
                    if (isCallSite(&*inst)) {
                        CallSite site(&*inst);
                        Function *called = getCalledFunction(site);

                        /* Check if it's an indirect call that couldn't be
                         * figured out by getCalledFunction().
                         * In this case, we can't do anything. */
                        if (!called) { continue; }

                        auto name        = called->getName();

                        if (name == "__kmpc_fork_call") {
                            forks.push_back(&*inst);
                            continue;
                        }

                        auto search_a    = allocFnSet.find(name);
                        if (search_a != allocFnSet.end()) {
                            alloc_sites.push_back(&*inst);
                        }

                        auto search_c    = callocFnSet.find(name);
                        if (search_c != callocFnSet.end()) {
                            calloc_sites.push_back(&*inst);
                        }

                        auto search_r    = reallocFnSet.find(name);
                        if (search_r != reallocFnSet.end()) {
                            realloc_sites.push_back(&*inst);
                        }

                        auto search_d    = dallocFnSet.find(name);
                        if (search_d != dallocFnSet.end()) {
                            dealloc_sites.push_back(&*inst);
                        }
                    }
                }
            }
        }

        /* Build the instrumentation function bindings. */
        Function *alloc_inst_fn          = declareAllocInstFn(); 
        Function *realloc_inst_fn        = declareReallocInstFn(); 
        Function *dealloc_inst_fn        = declareDeallocInstFn(); 
        Function *parallel_enter_inst_fn = declareParallelEnterInstFn(); 
        Function *parallel_exit_inst_fn  = declareParallelExitInstFn(); 

        /* Add the instrumentation. */
        uint64_t region_id = 0;
        for (Instruction *inst : forks) {
            uint64_t region_site = region_hash(region_id++); 

            output_source_location(CallSite(inst), region_site, 1);

            auto id_val = builder->getInt64(region_site);

            builder->SetInsertPoint(inst);
            builder->CreateCall(parallel_enter_inst_fn, { id_val });

            builder->SetInsertPoint(inst->getNextNode());

            builder->CreateCall(parallel_exit_inst_fn, { id_val });
        }

        uint64_t site_id = 0;
        for (Instruction *inst : alloc_sites) {
            uint64_t alloc_site = site_hash(site_id);

            CallSite     site      (inst);
            Value       *ptr_val = inst;
            Value      *size_val = site.getArgument(0);
            Value        *id_val = builder->getInt64(alloc_site);

            BasicBlock *BB   = inst->getParent();
            Instruction *end = BB->getTerminator();

            if (end) {
                if (end == inst) {
                    /* Invoke is a terminator. */
                    if (InvokeInst *inv = dyn_cast<InvokeInst>(end)) {
                        builder->SetInsertPoint(inv->getNormalDest(), inv->getNormalDest()->getFirstInsertionPt());
                    }
                } else {
                    builder->SetInsertPoint(BB->getTerminator());
                }
            } else {
                builder->SetInsertPoint(BB);
            }


            if (ptr_val->getType() != pointer_type) {
                ptr_val = builder->CreateBitCast(ptr_val, pointer_type);
            }
            if (size_val->getType() != int_type) {
                size_val = builder->CreateIntCast(size_val, int_type, false);
            }

            builder->CreateCall(alloc_inst_fn, { ptr_val, size_val, id_val });

            output_source_location(site, alloc_site, 0);

            site_id += 1;
        }
        
        for (Instruction *inst : calloc_sites) {
            uint64_t alloc_site = site_hash(site_id);

            CallSite site       (inst);
            Value   *ptr_val    = inst;
            Value   *n_elem_val = site.getArgument(0);
            Value   *size_val   = site.getArgument(1);
            Value   *id_val     = builder->getInt64(alloc_site);

            BasicBlock *BB   = inst->getParent();
            Instruction *end = BB->getTerminator();

            if (end) {
                if (end == inst) {
                    /* Invoke is a terminator. */
                    if (InvokeInst *inv = dyn_cast<InvokeInst>(end)) {
                        builder->SetInsertPoint(inv->getNormalDest(), inv->getNormalDest()->getFirstInsertionPt());
                    }
                } else {
                    builder->SetInsertPoint(BB->getTerminator());
                }
            } else {
                builder->SetInsertPoint(BB);
            }


            if (ptr_val->getType() != pointer_type) {
                ptr_val = builder->CreateBitCast(ptr_val, pointer_type);
            }
            if (n_elem_val->getType() != int_type) {
                size_val = builder->CreateIntCast(n_elem_val, int_type, false);
            }
            if (size_val->getType() != int_type) {
                size_val = builder->CreateIntCast(size_val, int_type, false);
            }

            size_val = builder->CreateMul(size_val, n_elem_val);

            builder->CreateCall(alloc_inst_fn, { ptr_val, size_val, id_val });
            
            output_source_location(site, alloc_site, 0);
            
            site_id += 1;
        }
        
        for (Instruction *inst : realloc_sites) {
            uint64_t alloc_site = site_hash(site_id);

            CallSite  site          (inst);
            Value    *ptr_val     = inst;
            Value    *old_ptr_val = site.getArgument(0);
            Value    *size_val    = site.getArgument(1);
            Value    *id_val      = builder->getInt64(alloc_site);

            BasicBlock *BB   = inst->getParent();
            Instruction *end = BB->getTerminator();

            if (end) {
                if (end == inst) {
                    /* Invoke is a terminator. */
                    if (InvokeInst *inv = dyn_cast<InvokeInst>(end)) {
                        builder->SetInsertPoint(inv->getNormalDest(), inv->getNormalDest()->getFirstInsertionPt());
                    }
                } else {
                    builder->SetInsertPoint(BB->getTerminator());
                }
            } else {
                builder->SetInsertPoint(BB);
            }


            if (ptr_val->getType() != pointer_type) {
                ptr_val = builder->CreateBitCast(ptr_val, pointer_type);
            }
            if (old_ptr_val->getType() != pointer_type) {
                old_ptr_val = builder->CreateBitCast(old_ptr_val, pointer_type);
            }
            if (size_val->getType() != int_type) {
                size_val = builder->CreateIntCast(size_val, int_type, false);
            }

            builder->CreateCall(realloc_inst_fn, { old_ptr_val, ptr_val, size_val, id_val });
            
            output_source_location(site, alloc_site, 0);

            site_id += 1;
        }
        
        for (Instruction *inst : dealloc_sites) {
            CallSite     site   (inst);
            Value    *ptr_val = site.getArgument(0);

            BasicBlock *BB   = inst->getParent();
            Instruction *end = BB->getTerminator();

            if (end) {
                if (end == inst) {
                    /* Invoke is a terminator. */
                    if (InvokeInst *inv = dyn_cast<InvokeInst>(end)) {
                        builder->SetInsertPoint(inv->getNormalDest(), inv->getNormalDest()->getFirstInsertionPt());
                    }
                } else {
                    builder->SetInsertPoint(BB->getTerminator());
                }
            } else {
                builder->SetInsertPoint(BB);
            }

            if (ptr_val->getType() != pointer_type) {
                ptr_val = builder->CreateBitCast(ptr_val, pointer_type);
            }

            builder->CreateCall(dealloc_inst_fn, { ptr_val });
        }
    }
    ////////////////////////////////////////////////////////////////////////////////
    
    //////////////////////////////////////////////////////////////// instrumentReads
    ////////////////////////////////////////////////////////////////////////////////
    void instrumentReads() {
        Type * pointer_type    = Type::getInt8PtrTy(theModule->getContext());
        Function *read_inst_fn = declareReadInstFn();

        std::vector<LoadInst*> reads;

        for (auto & F : *theModule)
        for (auto & BB : F)
        for (auto it = BB.begin(); it != BB.end(); it++)
            if (LoadInst* load = dyn_cast<LoadInst>(&*it))
                reads.push_back(load);

        for (LoadInst * read : reads) {
            Value            *addr = read->getOperand(0);
            AllocaInst     *alloca = dyn_cast<AllocaInst>(addr);
            GlobalVariable *global = dyn_cast<GlobalVariable>(addr);

            /* Try to limit the addresses we look at to heap regions.
             * Can't filter all non-heap, but this will help. */
            if (!alloca && !global) {
                builder->SetInsertPoint(read->getNextNode());
                Value * addr = builder->CreateBitCast(read->getOperand(0), pointer_type);

                std::vector<Value*> call_args { addr };
                builder->CreateCall(read_inst_fn, call_args);
            }
        }
    }
    ////////////////////////////////////////////////////////////////////////////////
    
    /////////////////////////////////////////////////////////////// instrumentWrites
    ////////////////////////////////////////////////////////////////////////////////
    void instrumentWrites() {
        Type * pointer_type     = Type::getInt8PtrTy(theModule->getContext());
        Function *write_inst_fn = declareWriteInstFn();

        std::vector<StoreInst*> writes;

        for (auto & F : *theModule)
        for (auto & BB : F)
        for (auto it = BB.begin(); it != BB.end(); it++)
            if (StoreInst* store = dyn_cast<StoreInst>(&*it))
                writes.push_back(store);

        for (StoreInst * write : writes) {
            Value            *addr = write->getOperand(1);
            AllocaInst     *alloca = dyn_cast<AllocaInst>(addr);
            GlobalVariable *global = dyn_cast<GlobalVariable>(addr);

            /* Try to limit the addresses we look at to heap regions.
             * Can't filter all non-heap, but this will help. */
            if (!alloca && !global) {
                builder->SetInsertPoint(write->getNextNode());
                Value * addr = builder->CreateBitCast(write->getOperand(1), pointer_type);

                std::vector<Value*> call_args { addr };
                builder->CreateCall(write_inst_fn, call_args);
            }
        }

    }
    ////////////////////////////////////////////////////////////////////////////////


    //////////////////////////////////////////////////////////////////// runOnModule
    ////////////////////////////////////////////////////////////////////////////////
    bool runOnModule(Module & module) {
        theModule = &module;
        IRBuilder<> my_builder(theModule->getContext());
        builder = &my_builder;

        source_location_file = fopen("region_info_sites.txt", "a");

        transformCallSites();

        fclose(source_location_file);

        return true;
    }
    ////////////////////////////////////////////////////////////////////////////////
};

} // namespace
char region_info::ID = 0;

static void registerMyPass(const PassManagerBuilder &,
                           legacy::PassManagerBase &PM) {
    PM.add(new region_info());
}

static RegisterStandardPasses RegisterMyPass(PassManagerBuilder::EP_ModuleOptimizerEarly, registerMyPass);
static RegisterStandardPasses RegisterMyPass0(PassManagerBuilder::EP_EnabledOnOptLevel0, registerMyPass);
