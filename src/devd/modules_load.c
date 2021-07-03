#ifdef ENABLE_KMOD
#include<ctype.h>
#include<dirent.h>
#include<stdlib.h>
#include<string.h>
#include"str.h"
#include"devd.h"
#include"system.h"
#include"logger.h"
#include"defines.h"
#include"pathnames.h"
#define TAG "modules"
#define DIRNAME "modules-load.d"
#define CONFEXT ".conf"

static const char*path[]={
	_PATH_ETC"/"DIRNAME,
	_PATH_USR_LOCAL_LIB"/"DIRNAME,
	_PATH_USR_LIB"/"DIRNAME,
	_PATH_USR_LOCAL_LIB"/"DIRNAME,
	_PATH_RUN"/"DIRNAME,
	NULL
};

int mods_conf_parse_file(const char*name,const char*file){
	if(!is_file(file)||!name)return -errno;
	int fd=open(file,O_RDONLY);
	if(fd<0)return telog_warn("open file %s failed",file);
	char r[2],buf[BUFSIZ];
	size_t idx=0;
	while(read(fd,&r,1)==1)if(isspace(r[0])){
		if(idx<=0)continue;
		buf[idx]=0,idx=0;
		tlog_debug("load %s from %s",buf,name);
		if(insmod(buf,true)<0)
			telog_warn("load %s failed",buf);
	}else if(r[0]=='#')skips(fd,"\r\n");
	else buf[idx++]=r[0];
	close(fd);
	return 0;
}

int mods_conf_parse_folder(const char*dir){
	if(!is_folder(dir))return -errno;
	DIR*d=opendir(dir);
	if(!d)return telog_warn("open folder %s failed",dir);
	struct dirent*e;
	size_t ds=strlen(dir),cs=strlen(CONFEXT);
	while((e=readdir(d))){
		size_t es=strlen(e->d_name);
		if(e->d_type!=DT_REG)continue;
		if(es<cs+1||e->d_name[0]=='.')continue;
		if(strncmp(
			e->d_name+(es-cs),
			CONFEXT,cs
		)!=0)continue;
		char*n=malloc(ds+es+3);
		if(!n)continue;
		snprintf(
			n,ds+es+2,
			"%s%s%s",
			dir,
			dir[ds-1]=='/'?"":"/",
			e->d_name
		);
		mods_conf_parse_file(e->d_name,n);
		free(n);
	}
	closedir(d);
	return 0;
}

int mods_conf_parse(){
	for(int i=0;path[i];i++)
		mods_conf_parse_folder(path[i]);
	return 0;
}
#endif
