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

#ifndef __NV50_IR_SCHED_H__
#define __NV50_IR_SCHED_H__

#include <list>
#include <vector>

#include "codegen/nv50_ir.h"

namespace nv50_ir {

class SchedNode {
public:
   SchedNode(Instruction *inst);
   ~SchedNode();

   Instruction *inst;
   std::vector<SchedNode *> childList;
   std::list<SchedNode *> parentList;
   int childCount;
   int parentCount;
   int depth;
   int preferredIssueCycle;
   int cost;
};

class Scheduler : public Pass {
public:
   //Scheduler();
   //~Scheduler();

private:
   typedef std::list<SchedNode *>::iterator NodeIter;
   typedef std::vector<SchedNode *>::iterator NodeVecIter;
   typedef std::list<SchedNode *>::reverse_iterator NodeRIter;

   bool visit(BasicBlock *bb);

   void addInstructions();
   void addDep(SchedNode *before, SchedNode *after);
   void calcDeps();
   void calcDepth();
   void initCandidateList();
   void emptyBB();
   NodeIter bestInst(NodeIter a, NodeIter b);
   SchedNode *selectInst();

   inline bool isValueReg(Value *v) const;
   inline bool isValueWMem(Value *v) const;
   inline bool instIsLoad(Instruction *i) const;
   inline int cost(Instruction *i) const;

   std::list<SchedNode *> nodeList;
   std::list<SchedNode *> candidateList;

   BasicBlock *bb;
   int cycle;
   const Target *target;
};

} // namespace nv50_ir

#endif // __NV50_IR_SCHED_H__
