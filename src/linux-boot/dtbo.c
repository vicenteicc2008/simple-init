/*
 *
 * Copyright (C) 2021 BigfootACA <bigfoot@classfun.cn>
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 */

#include<Uefi.h>
#include<Library/UefiLib.h>
#include<Library/BaseLib.h>
#include<Library/BaseMemoryLib.h>
#include<Library/MemoryAllocationLib.h>
#include<Library/UefiBootServicesTableLib.h>
#include<Protocol/SimpleFileSystem.h>
#include<Guid/Fdt.h>
#include<comp_libfdt.h>
#include<stdint.h>
#include"str.h"
#include"list.h"
#include"qcom.h"
#include"logger.h"
#include"fdtparser.h"
#include"internal.h"
#define TAG "dtbo"

static list*dtbos;
typedef struct dtbo_info{
	size_t id;
	fdt address;
	size_t size;
	size_t offset;
	char model[256];
	qcom_chip_info info;
	int64_t vote;
	dtbo_table_entry en;
	size_t en_offset;
}dtbo_info;

static bool sort_dtbo(list*f1,list*f2){
	LIST_DATA_DECLARE(d1,f1,dtbo_info*);
	LIST_DATA_DECLARE(d2,f2,dtbo_info*);
	return d1->vote<d2->vote;
}

static int select_dtbo(linux_boot*lb,dtbo_info*sel){
	int r;
	size_t fdt;
	char buf[64];
	tlog_info("select dtbo id %zu (%s)",sel->id,sel->model);
	fdt=MIN(lb->dtb.mem_size,lb->dtb.size+sel->size);
	if(fdt>lb->dtb.size){
		fdt_set_totalsize(lb->dtb.address,fdt);
		tlog_debug(
			"expand fdt size to %zu bytes (%s)",fdt,
			make_readable_str_buf(buf,sizeof(buf),fdt,1,0)
		);
		lb->dtb.size=fdt;
	}
	tlog_debug("try to apply dtb overlay...");
	r=fdt_overlay_apply(lb->dtb.address,sel->address);
	if(r!=0)tlog_error("apply overlay failed: %s",fdt_strerror(r));
	else tlog_debug("applied dtb overlay");
	return r;
}

static void process_dtbo(linux_boot*lb,qcom_chip_info*chip_info,dtbo_info*dtbo){
	char*model;
	int r,len=0;
	if((r=fdt_check_header(dtbo->address))!=0){
		tlog_warn(
			"dtbo id %zu invalid header: %s",
			dtbo->id,fdt_strerror(r)
		);
		dtbo->vote=INT64_MIN;
		return;
	}
	if(fdt_totalsize(dtbo->address)!=dtbo->size){
		tlog_warn("dtbo id %zu size mismatch",dtbo->id);
		dtbo->vote=INT64_MIN;
		return;
	}
	if(fdt_path_offset(dtbo->address,"/")!=0){
		tlog_warn("dtbo id %zu invalid root offset",dtbo->id);
		dtbo->vote=INT64_MIN;
		return;
	}
	model=(char*)fdt_getprop(dtbo->address,0,"model",&len);
	if(!model)model="Linux Device Tree Overlay";
	AsciiStrCpyS(dtbo->model,sizeof(dtbo->model),model);
	qcom_parse_id(dtbo->address,&dtbo->info);
	dtbo->vote=qcom_check_dtb(&dtbo->info,chip_info);
	lb->status.dtbo_id=(int64_t)dtbo->id;
	tlog_verbose(
		"dtbo id %zu offset %zu size %zu vote %lld (%s)",
		dtbo->id,dtbo->offset,dtbo->size,
		(long long)dtbo->vote,
		dtbo->model
	);
	list_obj_add_new_dup(&dtbos,dtbo,sizeof(dtbo_info));
}

static int read_qcom_dtbo(linux_boot*lb,linux_file_info*fi){
	int r=-1;
	list*f=NULL;
	dtbo_info dtbo;
	dtbo_info*sel=NULL;
	qcom_chip_info chip_info;
	dtbo_table_header hdr;
	uint32_t ens_cnt,ens_off,en_size,total_size,header_size;
	CopyMem(&hdr,fi->address,sizeof(dtbo_table_header));
	en_size=fdt32_to_cpu(hdr.dt_entry_size);
	ens_cnt=fdt32_to_cpu(hdr.dt_entry_count);
	ens_off=fdt32_to_cpu(hdr.dt_entry_offset);
	total_size=fdt32_to_cpu(hdr.total_size);
	header_size=fdt32_to_cpu(hdr.header_size);
	if(
		total_size<=0||
		total_size>MAX_DTBO_SIZE||
		total_size>=fi->size||
		header_size!=sizeof(dtbo_table_header)||
		en_size!=sizeof(dtbo_table_entry)
	)EDONE(tlog_error("invalid dtbo header"));
	if(qcom_get_chip_info(lb,&chip_info)!=0)goto done;
	tlog_info("found %u dtb overlays",ens_cnt);
	for(size_t s=0;s<ens_cnt;s++){
		ZeroMem(&dtbo,sizeof(dtbo_info));
		dtbo.id=s;
		dtbo.en_offset=ens_off+(en_size*s);
		CopyMem(&dtbo.en,fi->address+dtbo.en_offset,en_size);
		dtbo.offset=fdt32_to_cpu(dtbo.en.dt_offset);
		dtbo.address=fi->address+dtbo.offset;
		dtbo.size=fdt32_to_cpu(dtbo.en.dt_size);
		process_dtbo(lb,&chip_info,&dtbo);
	}
	if(lb->config&&lb->config->dtbo_id>=0){
		if((f=list_first(dtbos)))do{
			LIST_DATA_DECLARE(i,f,dtbo_info*);
			if(i&&i->id==lb->config->dtb_id)sel=i;
		}while((f=f->next));
		if(sel)tlog_verbose("use pre selected dtbo from config");
		else tlog_warn("specified dtbo id not found");
	}else{
		list_sort(dtbos,sort_dtbo);
		if(
			!(f=list_first(dtbos))||
			!(sel=LIST_DATA(f,dtbo_info*))
		)EDONE(tlog_warn("no dtbo found"));
		r=select_dtbo(lb,sel);
	}
	done:
	list_free_all_def(dtbos);
	dtbos=NULL;
	return r;
}

static int fi_free(void*data){
	if(!data)return 0;
	linux_file_clean((linux_file_info*)data);
	free(data);
	return 0;
}

int linux_boot_apply_dtbo(linux_boot*lb){
	list*f;
	size_t i=0;
	int r=-1;
	static const uint32_t dtbo_magic=MAGIC_DTBO;
	if(!lb->dtbo||!lb->dtb.address)return 0;
	if(lb->config&&lb->config->skip_dtbo){
		tlog_debug("skip load dtbo");
		r=0;
		goto done;
	}
	if((f=list_first(lb->dtbo)))do{
		i++;
		LIST_DATA_DECLARE(fi,f,linux_file_info*);
		int xr=-1;
		if(fi->size<=4||fi->size>MAX_DTBO_SIZE){
			tlog_error("invalid dtbo #%zu size",i);
			continue;
		}
		if(CompareMem(&fdt_magic,fi->address,4)==0){
			tlog_debug("found generic dtbo #%zu",i);
			lb->status.dtbo_id=0;
			xr=fdt_overlay_apply(lb->dtb.address,fi->address);
			if(xr!=0)tlog_error("apply overlay failed: %s",fdt_strerror(r));
		}else if(CompareMem(&dtbo_magic,fi->address,4)==0){
			tlog_debug("found qualcomm dtbo #%zu",i);
			if(fi->size<=sizeof(dtbo_table_header)){
				tlog_error("invalid dtbo #%zu size",i);
				continue;
			}
			xr=read_qcom_dtbo(lb,fi);
		}else{
			tlog_warn("unknown dtbo #%zu, skip",i);
			continue;
		}
		if(xr!=0)r=xr;
	}while((f=f->next));
	done:
	list_free_all(lb->dtbo,fi_free);
	lb->dtbo=NULL;
	return r;
}