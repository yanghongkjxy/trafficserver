/** @file

  A brief file description

  @section license License

  Licensed to the Apache Software Foundation (ASF) under one
  or more contributor license agreements.  See the NOTICE file
  distributed with this work for additional information
  regarding copyright ownership.  The ASF licenses this file
  to you under the Apache License, Version 2.0 (the
  "License"); you may not use this file except in compliance
  with the License.  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
 */

/****************************************************************************

  Basic Threads



**************************************************************************/
#include "P_EventSystem.h"

  ///////////////////////////////////////////////
  // Common Interface impl                     //
  ///////////////////////////////////////////////

static ink_thread_key init_thread_key();

ProxyMutex *global_mutex = NULL;
ink_hrtime
  Thread::cur_time = 0;
inkcoreapi ink_thread_key
  Thread::thread_data_key = init_thread_key();

Thread::Thread()
{
  mutex = new_ProxyMutex();
  mutex_ptr = mutex;
  MUTEX_TAKE_LOCK(mutex, (EThread *) this);
  mutex->nthread_holding = THREAD_MUTEX_THREAD_HOLDING;
}

static void
key_destructor(void *value)
{
  (void) value;
}

ink_thread_key
init_thread_key()
{
  ink_thread_key_create(&Thread::thread_data_key, key_destructor);
  return Thread::thread_data_key;
}

  ///////////////////////////////////////////////
  // Unix & non-NT Interface impl              //
  ///////////////////////////////////////////////

struct thread_data_internal
{
  ThreadFunction f;
  void *a;
  Thread *me;
};

static void *
spawn_thread_internal(void *a)
{
  thread_data_internal *p = (thread_data_internal *) a;
  p->me->set_specific();
  if (p->f)
    p->f(p->a);
  else
    p->me->execute();
  xfree(a);
  return NULL;
}

void
Thread::start(ThreadFunction f, void *a, size_t stacksize)
{
  if (0 == stacksize) {
    REC_ReadConfigInteger(stacksize, "proxy.config.thread.default.stacksize");
  }
  thread_data_internal *p = (thread_data_internal *) xmalloc(sizeof(thread_data_internal));
  p->f = f;
  p->a = a;
  p->me = this;
  this->tid = ink_thread_create(spawn_thread_internal, (void *) p, 0, stacksize);
}
