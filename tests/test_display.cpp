#define DISPLAY_CONFIG_NO_SDL
#include "display_config.h"
#include <cassert>
#include <initializer_list>
int main() {
    int w=640,h=480;
    setenv("TEST_RESOLUTION","auto",1);
    const int sizes[][2]={{640,480},{720,480},{720,720},{1024,768},{1280,720}};
    for (auto &size:sizes) { assert(display_config::select("TEST",size[0],size[1],w,h)); assert(w==size[0] && h==size[1]); }
    setenv("TEST_RESOLUTION","720x480",1);
    assert(display_config::select("TEST",640,480,w,h)); assert(w==720&&h==480);
    for (const char *bad:{"0x480","720x480oops","4097x480","wrong"}) {setenv("TEST_RESOLUTION",bad,1); assert(!display_config::select("TEST",640,480,w,h));}
    display_config::publish("TEST",720,480,true);
    assert(!strcmp(getenv("TEST_SCREEN_W"),"480")); assert(!strcmp(getenv("TEST_SCREEN_H"),"720"));
    display_config::publish("TEST",720,480,false);
    assert(!strcmp(getenv("TEST_SCREEN_W"),"720")); assert(!strcmp(getenv("TEST_SCREEN_H"),"480"));
}
