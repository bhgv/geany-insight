/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../../vendor/libvterm/include/vterm.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static char sent[4096];static size_t count;
static void output(const char *bytes,size_t length,void *user) { (void)user;assert(count+length<sizeof(sent));memcpy(sent+count,bytes,length);count+=length;sent[count]=0; }
static void feed(VTerm *vt,const char *text) { vterm_input_write(vt,text,strlen(text));vterm_screen_flush_damage(vterm_obtain_screen(vt)); }
static VTermScreenCell cell(VTerm *vt,int row,int col) { VTermScreenCell c;assert(vterm_screen_get_cell(vterm_obtain_screen(vt),(VTermPos){row,col},&c));return c; }
int main(void) {
 VTerm *vt=vterm_new(24,80);vterm_set_utf8(vt,1);VTermScreen *screen=vterm_obtain_screen(vt);vterm_screen_enable_altscreen(screen,1);vterm_screen_reset(screen,1);vterm_output_set_callback(vt,output,NULL);
 feed(vt,"\033[3");feed(vt,"1mRED\033[0m");VTermScreenCell c=cell(vt,0,0);assert(c.chars[0]=='R');vterm_screen_convert_color_to_rgb(screen,&c.fg);assert(c.fg.rgb.red>c.fg.rgb.green);
 feed(vt,"\rTWO\033[K");assert(cell(vt,0,0).chars[0]=='T');assert(cell(vt,0,2).chars[0]=='O');assert(!cell(vt,0,3).chars[0]);
 feed(vt,"\033[2;5H\033[38;2;1;2;3mX");c=cell(vt,1,4);assert(c.chars[0]=='X');vterm_screen_convert_color_to_rgb(screen,&c.fg);assert(c.fg.rgb.red==1 && c.fg.rgb.green==2 && c.fg.rgb.blue==3);
 feed(vt,"\033[?1049hALT\033[?1049l");assert(cell(vt,0,0).chars[0]=='T');assert(cell(vt,1,4).chars[0]=='X');
 feed(vt,"\033[3;1H\xe7\x95");feed(vt,"\x8c");assert(cell(vt,2,0).chars[0]==0x754c);assert(cell(vt,2,0).width==2);
 feed(vt,"\033[?2004h");vterm_keyboard_start_paste(vt);vterm_keyboard_end_paste(vt);assert(strstr(sent,"\033[200~") && strstr(sent,"\033[201~"));count=0;sent[0]=0;
 vterm_keyboard_key(vt,VTERM_KEY_UP,VTERM_MOD_NONE);assert(!strcmp(sent,"\033[A"));count=0;vterm_keyboard_unichar(vt,'c',VTERM_MOD_CTRL);assert(count==1 && sent[0]==3);
 vterm_free(vt);puts("Terminal core: fragmented SGR/UTF-8, truecolor, cursor/erase, alternate screen, wide cells, bracketed paste, arrows and Ctrl+C passed");return 0;
}
