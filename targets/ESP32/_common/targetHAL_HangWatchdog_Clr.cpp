//
// Copyright (c) .NET Foundation and Contributors
// See LICENSE file in the project root for full license information.
//

// LEDTREES: managed context for the hang watchdog's panic reason.
//
// The coredump keeps task stacks but not the managed heap in PSRAM, so a CLR
// stuck in native code cannot be traced back to the managed method from the
// dump alone. Before aborting, the watchdog asks for the current thread's
// frames and the exception being dispatched; the text becomes the panic reason
// stored in the coredump. Read while the CLR task is stuck on the other core:
// good enough for diagnostics, nothing here is written.

#include <stdio.h>
#include <nanoCLR_Runtime.h>

static int AppendType(char *buf, size_t size, int pos, const CLR_RT_TypeDef_Instance &type)
{
    if (pos >= (int)size || type.m_assm == NULL || type.m_target == NULL)
    {
        return pos;
    }

    const char *ns = type.m_assm->GetString(type.m_target->nameSpace);
    const char *name = type.m_assm->GetString(type.m_target->name);

    return pos + snprintf(buf + pos, size - pos, "%s%s%s", ns, ns[0] ? "." : "", name);
}

extern "C" void NF_HangWatchdog_DescribeClr(char *buf, size_t size)
{
    int pos = 0;
    CLR_RT_Thread *th = g_CLR_RT_ExecutionEngine.m_currentThread;

    if (th == NULL)
    {
        snprintf(buf, size, "no current thread");
        return;
    }

    pos += snprintf(buf + pos, size - pos, "pid %d", th->m_pid);

    if (th->m_nestedExceptionsPos > 0 && pos < (int)size)
    {
        CLR_RT_HeapBlock *ex = th->m_nestedExceptions[th->m_nestedExceptionsPos - 1].m_exception;
        CLR_RT_TypeDef_Instance exType;

        pos += snprintf(buf + pos, size - pos, " ex[%d] ", th->m_nestedExceptionsPos);
        if (ex != NULL && exType.InitializeFromIndex(ex->ObjectCls()))
        {
            pos = AppendType(buf, size, pos, exType);
        }
    }

    int depth = 0;
    for (CLR_RT_StackFrame *frame = th->CurrentFrame(); frame != NULL && frame->Caller() != NULL && depth < 8;
         frame = frame->Caller(), depth++)
    {
        if (pos >= (int)size)
        {
            break;
        }

        CLR_RT_MethodDef_Instance &md = frame->m_call;
        CLR_RT_TypeDef_Instance owner;

        // a frame being set up on the other core may not have its method yet
        if (md.m_assm == NULL || md.m_target == NULL)
        {
            pos += snprintf(buf + pos, size - pos, " < ?");
            continue;
        }

        pos += snprintf(buf + pos, size - pos, " < ");
        if (owner.InitializeFromMethod(md))
        {
            pos = AppendType(buf, size, pos, owner);
        }

        if (pos < (int)size)
        {
            pos += snprintf(
                buf + pos,
                size - pos,
                "::%s+%d",
                md.m_assm->GetString(md.m_target->name),
                frame->m_IP && frame->m_IPstart ? (int)(frame->m_IP - frame->m_IPstart) : -1);
        }
    }
}
