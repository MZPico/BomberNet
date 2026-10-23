/* start DH0:bomber as Workbench does on a double-click: LoadSeg, a new process with the icon's stack, the WBStartup
   message on its port; DH0:launch.txt says how far it got (the game's title on the screen is the rest of the test) */
#include <stdio.h>
#include <string.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/icon.h>
#include <workbench/startup.h>
#include <workbench/workbench.h>
#include <dos/dostags.h>

struct Library *IconBase;

int main(void) {
  FILE *f = fopen("DH0:launch.txt", "w");
  struct DiskObject *d;
  struct WBStartup *msg;
  struct MsgPort *reply;
  struct Process *proc;
  BPTR seg, dir;
  LONG stack;
  if (!f) return 20;
  IconBase = OpenLibrary("icon.library", 37);
  d = IconBase ? GetDiskObject((STRPTR)"DH0:bomber") : NULL;
  stack = d ? d->do_StackSize : 4096;
  if (stack < 4096) stack = 4096;
  fprintf(f, "icon stack %ld\n", (long)stack); fflush(f);
  seg = LoadSeg((STRPTR)"DH0:bomber");
  dir = Lock((STRPTR)"DH0:", ACCESS_READ);
  reply = CreateMsgPort();
  msg = AllocVec(sizeof(struct WBStartup) + sizeof(struct WBArg), MEMF_PUBLIC | MEMF_CLEAR);
  if (!seg || !dir || !reply || !msg) { fprintf(f, "setup failed: seg %lx dir %lx\n", (unsigned long)seg, (unsigned long)dir); fclose(f); return 20; }
  msg->sm_Message.mn_ReplyPort = reply;
  msg->sm_Message.mn_Length = sizeof(struct WBStartup);
  msg->sm_Segment = seg;
  msg->sm_NumArgs = 1;
  msg->sm_ArgList = (struct WBArg *)(msg + 1);
  msg->sm_ArgList[0].wa_Lock = dir;
  msg->sm_ArgList[0].wa_Name = (STRPTR)"bomber";
  proc = CreateNewProcTags(NP_Seglist, seg, NP_FreeSeglist, FALSE, NP_StackSize, stack, NP_Name, (ULONG)"bomber",
                           NP_CurrentDir, 0, NP_Input, 0, NP_Output, 0, NP_CloseInput, FALSE, NP_CloseOutput, FALSE, TAG_DONE);
  if (!proc) { fprintf(f, "CreateNewProc failed\n"); fclose(f); return 20; }
  msg->sm_Process = &proc->pr_MsgPort;
  PutMsg(&proc->pr_MsgPort, (struct Message *)msg);
  fprintf(f, "started\n"); fflush(f);
  WaitPort(reply);                                       /* the program replies when it ends */
  GetMsg(reply);
  fprintf(f, "replied: the program ended\n");
  fclose(f);
  UnLoadSeg(seg); UnLock(dir); FreeVec(msg); DeleteMsgPort(reply);
  if (d) FreeDiskObject(d);
  return 0;
}
