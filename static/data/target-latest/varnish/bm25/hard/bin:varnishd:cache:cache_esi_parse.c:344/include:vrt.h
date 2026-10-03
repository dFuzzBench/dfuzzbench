/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2021 Varnish Software AS
 * All rights reserved.
 *
 * Author: Poul-Henning Kamp <phk@phk.freebsd.dk>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * Runtime support for compiled VCL programs and VMODs.
 *
 * NB: When this file is changed, lib/libvcc/generate.py *MUST* be rerun.
 */

#ifdef CACHE_H_INCLUDED
#  error "vrt.h included after cache.h - they are inclusive"
#endif

#ifdef VRT_H_INCLUDED
#  error "vrt.h included multiple times"
#endif
#define VRT_H_INCLUDED

#ifndef VDEF_H_INCLUDED
#  error "include vdef.h before vrt.h"
#endif

#define VRT_MAJOR_VERSION	21U

#define VRT_MINOR_VERSION	0U

/***********************************************************************
 * Major and minor VRT API versions.
 *
 * Whenever something is added, increment MINOR version
 * Whenever something is deleted or changed in a way which is not
 * binary/load-time compatible, increment MAJOR version
 *
 * XX.X (unreleased)
 *	VRT_r_obj_stale_age() added
 *	VRT_r_obj_stale_can_esi() added
 *	VRT_r_obj_stale_grace() added
 *	VRT_r_obj_stale_hits() added
 *	VRT_r_obj_stale_keep() added
 *	VRT_r_obj_stale_proto() added
 *	VRT_r_obj_stale_reason() added
 *	VRT_r_obj_stale_status() added
 *	VRT_r_obj_stale_storage() added
 *	VRT_r_obj_stale_time() added
 *	VRT_r_obj_stale_ttl() added
 *	VRT_r_obj_stale_uncacheable() added
 *	enum gethdr_e has new value HDR_OBJ_STALE
 *	typedef hdr_t added
 *	struct gethdr_s.what changed to hdr_t
 *	VRT_VSC_Alloc() renamed to VRT_VSC_Allocv()
 *	new VRT_VSC_Alloc() added
 *	VRT_r_param_backend_idle_timeout() added
 *	VRT_r_param_backend_wait_limit() added
 *	VRT_r_param_backend_wait_timeout() added
 *	VRT_r_param_between_bytes_timeout() added
 *	VRT_r_param_connect_timeout() added
 *	VRT_r_param_default_grace() added
 *	VRT_r_param_default_keep() added
 *	VRT_r_param_default_ttl() added
 *	VRT_r_param_first_byte_timeout() added
 *	VRT_r_param_idle_send_timeout() added
 *	VRT_r_param_max_esi_depth() added
 *	VRT_r_param_max_restarts() added
 *	VRT_r_param_max_retries() added
 *	VRT_r_param_pipe_task_deadline() added
 *	VRT_r_param_pipe_timeout() added
 *	VRT_r_param_send_timeout() added
 *	VRT_r_param_shortlived() added
 *	VRT_r_param_timeout_idle() added
 *	VRT_r_param_transit_buffer() added
 *	VRT_r_param_uncacheable_ttl() added
 *	VRT_AddVFP() removed
 *	VRT_AddVDP() removed
 *	VRT_RemoveVFP() removed
 *	VRT_RemoveVDP() removed
 *	struct vrt_blob magic added
 *	struct strands magic added;
 * 21.0 (2025-03-17)
 *	VRT_u_req_grace() added
 *	VRT_u_req_ttl() added
 *	VRT_r_req_filters() added
 *	VRT_l_req_filters() added
 *	VRT_r_bereq_filters() added
 *	VRT_l_bereq_filters() added
 * 20.1 (2024-11-08 7.6.1)
 *	VDI_EVENT_SICK added to enum vcl_event_e
 * 20.0 (2024-09-13)
 *	struct vrt_backend.backend_wait_timeout added
 *	struct vrt_backend.backend_wait_limit  added
 * 19.1 (2024-05-27)
 *	[cache_varnishd.h] ObjWaitExtend() gained statep argument
 * 19.0 (2024-03-18)
 *	[cache.h] (struct req).filter_list renamed to vdp_filter_list
 *	order of vcl/vmod and director COLD events reversed to directors first
 *	VRT_u_sess_idle_send_timeout() added
 *	VRT_u_sess_send_timeout() added
 *	VRT_u_sess_timeout_idle() added
 *	VRT_u_sess_timeout_linger() added
 *	(struct vrt_backend).*_timeout must be initialized to a negative value
 *	VRT_BACKEND_INIT() helper macro added
 *	VRT_l_bereq_task_deadline() added
 *	VRT_r_bereq_task_deadline() added
 *	VRT_u_bereq_task_deadline() added
 *	VRT_u_bereq_between_bytes_timeout() added
 *	VRT_u_bereq_connect_timeout() added
 *	VRT_u_bereq_first_byte_timeout() added
 * 18.1 (2023-12-05)
 *	vbf_objiterate() implementation changed #4013
 * 18.0 (2023-09-15)
 *	[cache_filter.h] struct vdp gained priv1 member
 *	VRT_trace() added
 * 17.0 (2023-03-15)
 *	VXID is 64 bit
 *	[cache.h] http_GetRange() changed
 *	exp_close added to struct vrt_backend_probe
 *	VRT_new_backend() signature changed
 *	VRT_new_backend_clustered() signature changed
 *	authority field added to struct vrt_backend
 *	release field added to struct vdi_methods
 * 16.0 (2022-09-15)
 *	VMOD C-prototypes moved into JSON
 *	VRT_AddVDP() deprecated
 *	VRT_AddVFP() deprecated
 *	VRT_RemoveVDP() deprecated
 *	VRT_RemoveVFP() deprecated
 * 15.0 (2022-03-15)
 *	VRT_r_req_transport() added
 *	VRT_Assign_Backend() added
 *	VRT_StaticDirector() added
 *	enum lbody_e changed
 *	- previous enum lbody_e values are defined as macros
 *	The following functions changed to take `const char *, BODY`:
 *	- VRT_l_beresp_body()
 *	- VRT_l_resp_body()
 *	BODY can either be a BLOB or a STRANDS, but only a STRANDS
 *	can take a non-NULL const char * prefix. The changes to BODY
 *	assignments doesn't break the ABI or the API.
 *	TOSTRAND() and TOSTRANDS() macros added
 *	[cache.h] enum sess_close replaced by struct stream_close
 *	[cache.h] http_IsHdr() added
 *	[cache_filter.h] vfp_init_f() changed to take a VRT_CTX
 *	[cache_filter.h] vdp_init_f() changed to take a VRT_CTX
 *	[cache_filter.h] VRT_AddFilter() added
 *	[cache_filter.h] VRT_RemoveFilter() added
 * 14.0 (2021-09-15)
 *	VIN_n_Arg() no directly returns the directory name.
 *	VSB_new() and VSB_delete() removed
 *	VCL_STRINGLIST, vrt_magic_string_end removed
 *	VRT_String(), VRT_StringList(), VRT_CollectString() removed
 *	VRT_CollectStrands() renamed to VRT_STRANDS_string()
 *	The following functions changed to take `const char *, STRANDS`:
 *	- VRT_l_client_identity()
 *	- VRT_l_req_method()
 *	- VRT_l_req_url()
 *	- VRT_l_req_proto()
 *	- VRT_l_bereq_method()
 *	- VRT_l_bereq_url()
 *	- VRT_l_bereq_proto()
 *	- VRT_l_beresp_body()
 *	- VRT_l_beresp_proto()
 *	- VRT_l_beresp_reason()
 *	- VRT_l_beresp_storage_hint()
 *	- VRT_l_beresp_filters()
 *	- VRT_l_resp_body()
 *	- VRT_l_resp_proto()
 *	- VRT_l_resp_reason()
 *	- VRT_l_resp_filters()
 *	- VRT_SetHdr()
 *	VRT_UnsetHdr() added
 *	vrt_magic_string_unset removed (use VRT_UnsetHdr() instead)
 *	VNUMpfx() removed, SF_Parse_{Integer|Decimal|Number} added
 *	vrt_null_strands added
 *	vrt_null_blob added
 *	VRT_NULL_BLOB_TYPE added as the .type of vrt_null_blob
 *	VRT_blob() changed to return vrt_null_blob for
 *	    len == 0 or src == NULL arguments
 *	[cache.h] WS_Front() removed
 *	[cache.h] WS_Inside() removed
 *	[cache.h] WS_Assert_Allocated() removed
 *	[cache.h] WS_Allocated() added
 *	[cache.h] WS_Dump() added
 * 13.0 (2021-03-15)
 *	Move VRT_synth_page() to deprecated status
 *	Add VRT_synth_strands() and VRT_synth_blob()
 *	struct vrt_type now produced by generate.py
 *	VRT_acl_log() moved to VPI_acl_log()
 *	VRT_Endpoint_Clone() added.
 *	Calling convention for VDP implementation changed
 *	VRT_ValidHdr() added.
 *	struct vmod_priv_methods added
 *	struct vmod_priv free member replaced with methods
 *	VRT_CTX_Assert() added
 *	VRT_ban_string() signature changed
 *	VRT_priv_task_get() added
 *	VRT_priv_top_get() added
 *	VRT_re_init removed
 *	VRT_re_fini removed
 *	VRT_re_match signature changed
 *	VRT_regsub signature changed
 *	VRT_call() added
 *	VRT_check_call() added
 *	VRT_handled() added
 * 12.0 (2020-09-15)
 *	Added VRT_DirectorResolve()
 *	Added VCL_STRING VRT_BLOB_string(VRT_CTX, VCL_BLOB)
 *	[cache.h] WS_Reserve() removed
 *	[cache.h] WS_Printf() changed
 *	[cache.h] WS_ReservationSize() added
 *	[cache.h] WS_Front() deprecated
 *	[cache.h] WS_Reservation() added
 * 11.0 (2020-03-16)
 *	Changed type of vsa_suckaddr_len from int to size_t
 *	New prefix_{ptr|len} fields in vrt_backend
 *	VRT_HashStrands32() added
 *	VRT_l_resp_body() changed
 *	VRT_l_beresp_body() changed
 *	VRT_Format_Proxy() added	// transitional interface
 *	VRT_AllocStrandsWS() added
 * 10.0 (2019-09-15)
 *	VRT_UpperLowerStrands added.
 *	VRT_synth_page now takes STRANDS argument
 *	VRT_hashdata() now takes STRANDS argument
 *	VCL_BOOL VRT_Strands2Bool(VCL_STRANDS) added.
 *	VRT_BundleStrands() moved to vcc_interface.h
 *	VRT_VCL_{Busy|Unbusy} changed to VRT_VCL_{Prevent|Allow}_Cold
 *	VRT_re[fl]_vcl changed to VRT_VCL_{Prevent|Allow}_Discard
 *	VRT_Vmod_{Init|Unload} moved to vcc_interface.h
 *	VRT_count moved to vcc_interface.h
 *	VRT_VCL_Prevent_Cold() and VRT_VCL_Allow_Cold() added.
 *	VRT_vcl_get moved to vcc_interface.h
 *	VRT_vcl_rel moved to vcc_interface.h
 *	VRT_vcl_select moved to vcc_interface.h
 *	VRT_VSA_GetPtr() changed
 *	VRT_ipcmp() changed
 *	VRT_Stv_*() functions renamed to VRT_stevedore_*()
 *	[cache.h] WS_ReserveAll() added
 *	[cache.h] WS_Reserve(ws, 0) deprecated
 * 9.0 (2019-03-15)
 *	Make 'len' in vmod_priv 'long'
 *	HTTP_Copy() removed
 *	HTTP_Dup() added
 *	HTTP_Clone() added
 *	VCL_BLOB changed to newly introduced struct vrt_blob *
 *	VRT_blob() changed
 *	req->req_bodybytes removed
 *	    use: AZ(ObjGetU64(req->wrk, req->body_oc, OA_LEN, &u));
 *	struct vdi_methods .list callback signature changed
 *	VRT_LookupDirector() added
 *	VRT_SetChanged() added
 *	VRT_SetHealth() removed
 *	// in cache_filter.h:
 *	VRT_AddVDP() added
 *	VRT_RemoveVDP() added
 * 8.0 (2018-09-15)
 *	VRT_Strands() added
 *	VRT_StrandsWS() added
 *	VRT_CollectStrands() added
 *	VRT_STRANDS_string() removed from vrt.h (never implemented)
 *	VRT_Vmod_Init signature changed
 *	VRT_Vmod_Fini changed to VRT_Vmod_Unload
 *	// directors
 *	VRT_backend_healthy() removed
 *	VRT_Healthy() changed prototype
 *	struct vdi_methods and callback prototypes added
 *	struct director added;
 *	VRT_AddDirector() added
 *	VRT_SetHealth() added
 *	VRT_DisableDirector() added
 *	VRT_DelDirector() added
 *	// in cache_filter.h:
 *	VRT_AddVFP() added
 *	VRT_RemoveVFP() added
 * 7.0 (2018-03-15)
 *	lots of stuff moved from cache.h to cache_varnishd.h
 *	   (ie: from "$Abi vrt" to "$Abi strict")
 *	VCL_INT and VCL_BYTES are always 64 bits.
 *	path field added to struct vrt_backend
 *	VRT_Healthy() added
 *	VRT_VSC_Alloc() added
 *	VRT_VSC_Destroy() added
 *	VRT_VSC_Hide() added
 *	VRT_VSC_Reveal() added
 *	VRT_VSC_Overhead() added
 *	struct director.event added
 *	struct director.destroy added
 *	VRT_r_beresp_storage_hint() VCL <= 4.0  #2509
 *	VRT_l_beresp_storage_hint() VCL <= 4.0  #2509
 *	VRT_blob() added
 *	VCL_STRANDS added
 * 6.1 (2017-09-15 aka 5.2)
 *	http_CollectHdrSep added
 *	VRT_purge modified (may fail a transaction, signature changed)
 *	VRT_r_req_hash() added
 *	VRT_r_bereq_hash() added
 * 6.0 (2017-03-15):
 *	VRT_hit_for_pass added
 *	VRT_ipcmp added
 *	VRT_Vmod_Init signature changed
 *	VRT_vcl_lookup removed
 *	VRT_fail added
 *	[cache.h] WS_Reset and WS_Snapshot signatures changed
 *	[cache.h] WS_Front added
 *	[cache.h] WS_ReserveLumps added
 *	[cache.h] WS_Inside added
 *	[cache.h] WS_Assert_Allocated added
 * 5.0:
 *	Varnish 5.0 release "better safe than sorry" bump
 * 4.0:
 *	VCL_BYTES changed to long long
 *	VRT_CacheReqBody changed signature
 * 3.2:
 *	vrt_backend grew .proxy_header field
 *	vrt_ctx grew .sp field.
 *	vrt_acl type added
 */

/***********************************************************************/

#include <stddef.h>		// NULL, size_t
#include <stdint.h>		// [u]int%d_t

struct busyobj;
struct director;
struct http;
struct lock;
struct req;
struct stevedore;
struct stream_close;
struct suckaddr;
struct vcl;
struct vcldir;
struct VCL_conf;
struct vcl_sub;
struct vmod;
struct vmod_priv;
struct vrt_acl;
struct vsb;
struct VSC_main;
struct vsc_seg;
struct vsl_log;
struct vsmw_cluster;
struct wrk_vpi;
struct ws;

typedef const struct stream_close *stream_close_t;

/***********************************************************************
 * VCL_STRANDS:
 *
 * An argc+argv type of data structure where n indicates the number of
 * strings in the p array. Individual components of a strands may be null.
 *
 * A STRANDS allows you to work on a strings concatenation with the
 * option to collect it into a single STRING, or if possible work
 * directly on individual parts.
 *
 * The memory management is very strict: a VMOD function receiving a
 * STRANDS argument should keep no reference after the function returns.
 * Retention of a STRANDS further in the ongoing task is undefined
 * behavior and may result in a panic or data corruption.
 */

struct strands