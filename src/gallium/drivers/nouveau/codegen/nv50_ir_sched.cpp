/*
 * Copyright 2017 Boyan Ding
 * Copyright 2017 Roy Spliet
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
#include "codegen/nv50_ir_target.h"

namespace nv50_ir {

SchedNode::SchedNode(Instruction *inst)
{
   this->inst = inst;
   this->childCount = 0;
   this->parentCount = 0;
   this->depth = -1;
   this->preferredIssueCycle = 0;
   this->cost = 0;
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
      node->cost = cost(insn);
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

void Scheduler::calcDepth()
{
   std::list<SchedNode *> workList;

   for (NodeIter n = nodeList.begin(); n != nodeList.end(); ++n) {
      SchedNode *node = *n;

      if (node->childCount == 0) {
         node->depth = 0;
         workList.push_back(node);
      }
   }

   while (!workList.empty()) {
      SchedNode *node = workList.front();
      int depth = node->depth + 1;
      workList.pop_front();

      for (NodeIter p = node->parentList.begin(); p != node->parentList.end(); ++p) {
         SchedNode *parent = *p;

         if (parent->depth < depth) {
            parent->depth = depth;
            workList.push_back(parent);
         }
      }
   }

   /*
   for (NodeIter n = nodeList.begin(); n != nodeList.end(); ++n) {
      SchedNode *node = *n;

      INFO("Depth: %i\n", node->depth);
   }
   */
}

void Scheduler::initCandidateList()
{
   candidateList.clear();

   for (NodeIter n = nodeList.begin(); n != nodeList.end(); ++n) {
      SchedNode *node = *n;

      if (!node->parentCount)
         candidateList.push_back(node);
   }
}

void Scheduler::emptyBB()
{
   Instruction *inst = bb->getEntry();

   while (inst != NULL) {
      bb->remove(inst);
      inst = bb->getEntry();
   }
}

Scheduler::NodeIter Scheduler::bestInst(NodeIter a, NodeIter b)
{
   SchedNode *nodeA;
   SchedNode *nodeB;
   SchedNode *lastNode;
   bool cyclePref;

   nodeA = *a;
   nodeB = *b;
   lastNode = nodeList.back();

   /* Keep BB flow where it is */
   if (nodeA->inst == lastNode->inst)
      return b;
   if (nodeB->inst == lastNode->inst)
      return a;

   cyclePref = (nodeA->preferredIssueCycle > this->cycle) |
               (nodeB->preferredIssueCycle > this->cycle);

   /* Best preferred issue cycle */
   if (cyclePref) {
      if (nodeA->preferredIssueCycle < nodeB->preferredIssueCycle)
         return a;
      else if (nodeA->preferredIssueCycle > nodeB->preferredIssueCycle)
         return b;
   }

   /* Highest cost first */
   if (nodeA->cost > nodeB->cost)
      return a;
   else if (nodeB->cost > nodeA->cost)
      return b;

   /* Highest depth */
   return nodeA->depth >= nodeB->depth ? a : b;
}

SchedNode *Scheduler::selectInst()
{
   NodeIter pick = candidateList.begin();
   NodeIter i = pick;
   SchedNode *node;

   for (++i; i != candidateList.end(); ++i) {
      pick = bestInst(pick, i);
   }

   node = *pick;

   for (NodeVecIter c = node->childList.begin(); c != node->childList.end(); ++c) {
      SchedNode *child = *c;
      child->parentCount--;
      child->preferredIssueCycle = this->cycle + node->cost;

      if (child->parentCount == 0)
         candidateList.push_back(child);
   }

   candidateList.erase(pick);

   return node;
}

bool Scheduler::visit(BasicBlock *bb)
{
   nodeList.clear();
   this->cycle = 0;
   this->bb = bb;
   this->target = bb->getProgram()->getTarget();

   addInstructions();
   calcDeps();
   calcDepth();
   initCandidateList();
   emptyBB();

   while (!candidateList.empty()) {
      INFO("Candidates: %lu\n", candidateList.size());
      //nodeList.erase(ni);

      SchedNode *node = selectInst();
      bb->insertTail(node->inst);
      this->cycle += target->getLatency(node->inst);
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

int Scheduler::cost(Instruction *inst) const
{
   switch (inst->op) {
   case OP_LOAD:
   case OP_TEX:
   case OP_TXB:
   case OP_TXL:
   case OP_TXF:
   case OP_TXQ:
   case OP_TXD:
   case OP_TXG:
   case OP_TXLQ:
   case OP_TEXCSAA:
   case OP_TEXPREP:
   case OP_VFETCH:
      return 400;
   case OP_LINTERP:
      return 450;
   case OP_ATOM:
      return 600;
   case OP_MOV:
      /* Let's pretend movs are free, to push them down */
      return 0;
   default:
      for (unsigned i = 0; inst->srcExists(i); i++) {
         Value *v = inst->getSrc(i);
         if (isValueWMem(v))
            return v->reg.file == FILE_MEMORY_LOCAL ? 40 : 400;
      }

      for (unsigned i = 0; inst->defExists(i); i++) {
         Value *v = inst->getDef(i);
         if (isValueWMem(v))
            return v->reg.file == FILE_MEMORY_LOCAL ? 25 : 50;
      }
   }

   return this->target->getThroughput(inst) + this->target->getLatency(inst);
}

bool Scheduler::instIsLoad(Instruction *i) const
{
   switch (i->op) {
   case OP_LOAD:
   case OP_ATOM:
   case OP_TEX:
   case OP_TXB:
   case OP_TXL:
   case OP_TXF:
   case OP_TXQ:
   case OP_TXD:
   case OP_TXG:
   case OP_TXLQ:
   case OP_TEXCSAA:
   case OP_TEXPREP:
   case OP_VFETCH:
   case OP_LINTERP:
      return true;
   default:
      return false;
   }
}

} // namespace nv50_ir

