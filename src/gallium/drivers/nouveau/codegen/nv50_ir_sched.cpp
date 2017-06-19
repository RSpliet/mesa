/*
 * Copyright 2017 Boyan Ding
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include "codegen/nv50_ir.h"
#include "codegen/nv50_ir_sched.h"

namespace nv50_ir {

SchedNode::SchedNode(Instruction *inst)
{
   this->inst = inst;
   this->childCount = 0;
   this->parentCount = 0;
   inst->snode = this;
}

SchedNode::~SchedNode()
{
   inst->snode = NULL;
}

void Scheduler::addInstructions()
{
   Instruction *insn, *next;

   for (insn = bb->getEntry(); insn != NULL; insn = next) {
      next = insn->next;
      SchedNode *node = new SchedNode(insn);
      nodeList.push_back(node);
   }
}

void Scheduler::addDep(SchedNode *before, SchedNode *after)
{
   before->childList.push_back(after);
   before->childCount++;
   after->parentList.push_back(before);
   after->parentCount++;
}

void Scheduler::calcDeps()
{
#define WRMEM_FILES (FILE_MEMORY_LOCAL - FILE_MEMORY_BUFFER)
#define WRMEM_ID(t) ((t) - FILE_MEMORY_BUFFER)
   SchedNode *lastMemWrite[WRMEM_FILES] = {};
   SchedNode *nextMemWrite[WRMEM_FILES] = {};
   for (NodeIter n = nodeList.begin(); n != nodeList.end(); ++n) {
      SchedNode *node = *n;
      Instruction *inst = node->inst;

      if (inst->fixed) {
         NodeIter n2;
         for (n2 = nodeList.begin(); n2 != n; ++n2) {
            addDep(*n2, *n);
         }
         for (++n2; n2 != nodeList.end(); ++n2) {
            addDep(*n, *n2);
         }
      }

      // Memory RAW
      for (unsigned i = 0; inst->srcExists(i); i++) {
         Value *v = inst->getSrc(i);
         if (isValueWMem(v)) {
            if (lastMemWrite[WRMEM_ID(v->reg.file)])
               addDep(lastMemWrite[WRMEM_ID(v->reg.file)], node);
         }
      }
      for (unsigned i = 0; inst->defExists(i); i++) {
         Value *v = inst->getDef(i);
         if (isValueReg(v)) {
            for (Value::UseIterator u = v->uses.begin(); u != v->uses.end(); ++u) {
               Instruction *useInst = (*u)->getInsn();
               if (useInst->bb != bb)
                  continue;
               SchedNode *useNode = useInst->snode;
               addDep(node, useNode);
            }
         } else if (isValueWMem(v)) {
            // Memory WAW
            if (lastMemWrite[WRMEM_ID(v->reg.file)])
               addDep(lastMemWrite[WRMEM_ID(v->reg.file)], node);
            lastMemWrite[WRMEM_ID(v->reg.file)] = node;
         }
      }
   }

   for (NodeRIter n = nodeList.rbegin(); n != nodeList.rend(); ++n) {
      // Memory WAR
      SchedNode *node = *n;
      Instruction *inst = node->inst;
      for (unsigned i = 0; inst->srcExists(i); i++) {
         Value *v = inst->getSrc(i);
         if (isValueWMem(v)) {
            if (nextMemWrite[WRMEM_ID(v->reg.file)]) {
               addDep(node, nextMemWrite[WRMEM_ID(v->reg.file)]);
            }
         }
      }
      for (unsigned i = 0; inst->defExists(i); i++) {
         Value *v = inst->getDef(i);
         if (isValueWMem(v)) {
            nextMemWrite[WRMEM_ID(v->reg.file)] = node;
         }
      }
   }
#undef WRMEM_FILES
#undef WRMEM_ID
}

void Scheduler::emptyBB()
{
   Instruction *inst = bb->getEntry();

   while (inst != NULL) {
      bb->remove(inst);
      inst = bb->getEntry();
   }
}

Scheduler::NodeIter Scheduler::chooseInst()
{
   return nodeList.begin();
}

bool Scheduler::visit(BasicBlock *bb)
{
   this->bb = bb;

   addInstructions();
   calcDeps();

   emptyBB();

   while (!nodeList.empty()) {
      //
      NodeIter ni = chooseInst();
      nodeList.erase(ni);

      SchedNode *node = *ni;
      bb->insertTail(node->inst);
      delete node;
   }

   return true;
}

bool Scheduler::isValueReg(Value *v) const
{
   switch (v->reg.file) {
   case FILE_GPR:
   case FILE_PREDICATE:
   case FILE_FLAGS:
   case FILE_ADDRESS:
      return true;
   default:
      return false;
   }
}

bool Scheduler::isValueWMem(Value *v) const
{
   switch (v->reg.file) {
   case FILE_MEMORY_BUFFER:
   case FILE_MEMORY_GLOBAL:
   case FILE_MEMORY_SHARED:
   case FILE_MEMORY_LOCAL:
      return true;
   default:
      return false;
   }
}

} // namespace nv50_ir

