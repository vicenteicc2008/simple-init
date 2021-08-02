#ifdef ENABLE_GUI
#include<errno.h>
#include<stdlib.h>
#include<unistd.h>
#include<signal.h>
#include<sys/time.h>
#include<semaphore.h>
#include"lvgl.h"
#include"logger.h"
#include"gui.h"
#include"font.h"
#include"sysbar.h"
#include"guidrv.h"
#define TAG "gui"
int gui_dpi=400,gui_font_size=24;
uint32_t gui_w=-1,gui_h=-1;
uint32_t gui_sw,gui_sh,gui_sx,gui_sy;
lv_font_t*gui_font=&lv_font_montserrat_24;
lv_font_t*symbol_font=NULL;
lv_group_t*gui_grp=NULL;
bool run=true;
bool gui_sleep=false;
static sem_t gui_wait;

void gui_do_quit(){
	guidrv_exit();
}

void gui_quit_sleep(){
	if(gui_sleep){
		gui_sleep=false;
		lv_tick_inc(LV_DISP_DEF_REFR_PERIOD);
		lv_task_handler();
		lv_disp_trig_activity(NULL);
		sem_post(&gui_wait);
	}
}

static void off_screen(int s __attribute__((unused))){
	tlog_debug("screen sleep");
	guidrv_set_brightness(0);
}

void guess_font_size(){
	if(gui_dpi>550)gui_font_size=72;
	else if(gui_dpi>450)gui_font_size=64;
	else if(gui_dpi>350)gui_font_size=48;
	else if(gui_dpi>250)gui_font_size=32;
	else if(gui_dpi>150)gui_font_size=24;
	else if(gui_dpi>50)gui_font_size=12;
	else if(gui_dpi>=0)gui_font_size=8;
	tlog_debug("select font size %d based on dpi",gui_font_size);
}

int gui_init(draw_func draw){
	lv_obj_t*screen;
	lv_init();
	gui_grp=lv_group_create();
	if(guidrv_init(&gui_w,&gui_h,&gui_dpi)<0)return -1;
	gui_sx=0,gui_sy=0,gui_sw=gui_w,gui_sh=gui_h;
	tlog_debug("driver init done");
	guess_font_size();
	#ifdef ENABLE_FREETYPE2
	lv_freetype_init(64,1,0);
	char*x=getenv("FONT");
	gui_font=x?
		lv_ft_init(x,24,0):
		lv_ft_init_rootfs(_PATH_ETC"/default.ttf",gui_font_size,0);
	x=getenv("FONT_SYMBOL");
	symbol_font=x?
		lv_ft_init(x,24,0):
		lv_ft_init_rootfs(_PATH_ETC"/symbols.ttf",gui_font_size,0);
	if(!symbol_font)
		telog_error("failed to load symbol font");
	#endif
	if(!gui_font)
		return terlog_error(-1,"failed to load font");
	lv_theme_t*th=LV_THEME_DEFAULT_INIT(
		LV_THEME_DEFAULT_COLOR_PRIMARY,
		LV_THEME_DEFAULT_COLOR_SECONDARY,
		LV_THEME_DEFAULT_FLAG,
		gui_font,gui_font,gui_font,gui_font
	);
	lv_theme_set_act(th);
	if(!(screen=lv_scr_act()))return trlog_error(-1,"failed to get screen");
	sysbar_draw(screen);
	draw(sysbar.content);
	sem_init(&gui_wait,0,0);
	while(run){
		if(lv_disp_get_inactive_time(NULL)<10000){
			lv_task_handler();
			guidrv_taskhandler();
		}else{
			int o=guidrv_get_brightness();
			if(o<=0)o=100;
			if(o>20)guidrv_set_brightness(20);
			alarm(20);
			signal(SIGALRM,off_screen);
			tlog_debug("enter sleep");
			gui_sleep=true;
			sem_wait(&gui_wait);
			gui_sleep=false;
			alarm(0);
			guidrv_set_brightness(o);
			tlog_debug("quit sleep");
		}
		usleep(30000);
	}
	gui_do_quit();
	return 0;
}

uint32_t custom_tick_get(void){
	errno=0;
	if(guidrv_get_driver()){
		uint32_t u=guidrv_tickget();
		if(errno==0)return u;
	}
	static uint64_t start_ms=0;
	if(start_ms==0){
		struct timeval tv_start;
		gettimeofday(&tv_start,NULL);
		start_ms=(tv_start.tv_sec*1000000+tv_start.tv_usec)/1000;
	}
	struct timeval tv_now;
	gettimeofday(&tv_now,NULL);
	return (uint64_t)((tv_now.tv_sec*1000000+tv_now.tv_usec)/1000)-start_ms;
}

#endif
