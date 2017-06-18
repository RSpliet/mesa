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

void Scheduler::calcDeps()
{
   /*
   for (NodeIter n = nodeList.begin(); n != nodeList.end(); ++n) {
      SchedNode *node = *n;
      Instruction *inst = node->inst;

      for (int i = 0; i < inst->defCount(); i++) {
         Value *v = inst->getDef(i);
         for (Value::UseIterator u = v->uses.begin(); u != v->uses.end(); ++v) {
            Instruction *dep = (*u)->getInsn();
         }
      }
   }
   */
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

} // namespace nv50_ir