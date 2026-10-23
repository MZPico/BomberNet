/* the icons read by icon.library, as Workbench reads them: their fields to DH0:icon.txt, their images drawn in a window */
#include <stdio.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/icon.h>
#include <proto/intuition.h>
#include <workbench/workbench.h>
#include <intuition/intuition.h>

struct Library *IconBase;
struct IntuitionBase *IntuitionBase;

static void report(FILE *f, const char *name, struct DiskObject *d) {
  struct Image *a, *b;
  if (!d) { fprintf(f, "%s: GetDiskObject failed, IoErr %ld\n", name, (long)IoErr()); return; }
  a = (struct Image *)d->do_Gadget.GadgetRender; b = (struct Image *)d->do_Gadget.SelectRender;
  fprintf(f, "%s: magic %04x version %u type %u stack %ld gadget %dx%d flags %04x tool '%s' image %dx%dx%d select %s",
          name, d->do_Magic, d->do_Version, d->do_Type, (long)d->do_StackSize, d->do_Gadget.Width, d->do_Gadget.Height,
          d->do_Gadget.Flags, d->do_DefaultTool ? (char *)d->do_DefaultTool : "-", a ? a->Width : -1, a ? a->Height : -1,
          a ? a->Depth : -1, b ? "yes" : "no");
  if (b) fprintf(f, " %dx%dx%d", b->Width, b->Height, b->Depth);
  fprintf(f, " drawer %s\n", d->do_DrawerData ? "yes" : "no");
}

int main(void) {
  FILE *f = fopen("DH0:icon.txt", "w");
  struct DiskObject *tool, *disk;
  struct Window *w;
  if (!f) return 20;
  IconBase = OpenLibrary("icon.library", 37);
  IntuitionBase = (struct IntuitionBase *)OpenLibrary("intuition.library", 37);
  if (!IconBase || !IntuitionBase) { fprintf(f, "no icon.library %p or intuition %p\n", IconBase, IntuitionBase); fclose(f); return 20; }
  fprintf(f, "icon.library version %u.%u\n", IconBase->lib_Version, IconBase->lib_Revision);
  tool = GetDiskObject((STRPTR)"DH0:bomber");
  disk = GetDiskObject((STRPTR)"DH0:disk");
  report(f, "bomber.info", tool);
  report(f, "disk.info", disk);
  fflush(f);
  w = OpenWindowTags(NULL, WA_Left, 20, WA_Top, 20, WA_Width, 400, WA_Height, 120, WA_Title, (ULONG)"icons", TAG_DONE);
  if (w) {
    if (tool) {
      DrawImage(w->RPort, (struct Image *)tool->do_Gadget.GadgetRender, 20, 30);
      if (tool->do_Gadget.SelectRender) DrawImage(w->RPort, (struct Image *)tool->do_Gadget.SelectRender, 120, 30);
    }
    if (disk) DrawImage(w->RPort, (struct Image *)disk->do_Gadget.GadgetRender, 230, 30);
    fprintf(f, "drawn\n");
    fclose(f); f = NULL;
    Delay(50 * 60);
    CloseWindow(w);
  }
  if (f) fclose(f);
  if (tool) FreeDiskObject(tool);
  if (disk) FreeDiskObject(disk);
  return 0;
}
