
#include "retrostore.h"
#include "load.h"
#include "inout.h"
#include "esp.h"
#include "trs-lib.h"

typedef void (*pc_t)();

#define FILE_NAME_LEN 30

static char file_name[FILE_NAME_LEN + 1] = "";

static form_item_t form_load_items[] = {
  FORM_ITEM_INPUT("Filename", file_name, sizeof(file_name) - 1, 0, NULL),
  FORM_ITEM_END
};

static form_t form_load = {
	.title = "Load XRAY State",
	.form_items = form_load_items
};


static void load()
{
  int i = 0;
  uint8_t b;
  uint16_t len;
  pc_t pc;
  uint8_t l, h;
  uint8_t* video = (uint8_t*) 0x3c00;
  
  out31(TRS_IO_CORE_MODULE_ID);
  out31(TRS_IO_LOAD_XRAY_STATE);
  do {
    out31(file_name[i]);
  } while(file_name[i++] != '\0');

  wait_for_esp();

  b = in31();
  if (b != 0) goto err;
  l = in31();
  h = in31();
  pc = (pc_t) (l | (h << 8));
  l = in31();
  h = in31();
  len = (l | (h << 8));
  if (len != 1024) goto err;

  for(i = 0; i < len; i++) {
    *video++ = in31();
  }

  (*pc)();
  
get_key();
  return;

err:
  wnd_popup("Error loading XRAY state");
get_key();
  
}

void load_xray_state()
{
  if (form(&form_load, false) == FORM_ABORT) {
    return;
  }
  load();
}
