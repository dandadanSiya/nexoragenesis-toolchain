/* BOOTSTRAP_C_TEST, QEMU/OVMF ONLY. External C test consumer, not the OS.
 * No StartImage: NTASM entry is a pure ms_abi uint64_t function, not EFI_STATUS.
 */
#include "pe_validate.h"
typedef uint64_t U64; typedef uint64_t STATUS; typedef void *HANDLE;
typedef uint16_t CHAR16; typedef uint64_t UINTN;
#define EFIAPI __attribute__((ms_abi))
#define ERROR(s) (((s)>>63)!=0)
#define EFI_ERROR(n) (UINT64_C(0x8000000000000000)|(n))
typedef struct {uint32_t a;uint16_t b,c;uint8_t d[8];} GUID;
typedef struct {uint64_t sig;uint32_t rev,size,crc,res;} HEADER;
typedef struct FILE FILE;
struct FILE {
 U64 rev; STATUS(EFIAPI *open)(FILE*,FILE**,CHAR16*,U64,U64);
 STATUS(EFIAPI *close)(FILE*); void *delete_file;
 STATUS(EFIAPI *read)(FILE*,UINTN*,void*);void *write,*get_position;
 STATUS(EFIAPI *set_position)(FILE*,U64);
 void *get_info,*set_info,*flush;
};
typedef struct {U64 rev;STATUS(EFIAPI *open_volume)(void*,FILE**);} FS;
typedef struct {
 uint32_t rev; HANDLE parent;void *system;HANDLE device;void *path,*reserved;
 uint32_t options_size;void *options;void *base;U64 size;
 uint32_t code_type,data_type;void *unload;
} LOADED;
typedef struct {
 HEADER hdr;void *raise_tpl,*restore_tpl,*allocate_pages,*free_pages,*get_memory_map;
 STATUS(EFIAPI *allocate_pool)(uint32_t,UINTN,void**); STATUS(EFIAPI *free_pool)(void*);
 void *create_event,*set_timer,*wait_event,*signal_event,*close_event,*check_event;
 void *install_protocol,*reinstall_protocol,*uninstall_protocol;
 STATUS(EFIAPI *handle_protocol)(HANDLE,GUID*,void**);
 void *reserved,*register_protocol_notify,*locate_handle,*locate_device_path,*install_config_table;
 STATUS(EFIAPI *load_image)(uint8_t,HANDLE,void*,void*,UINTN,HANDLE*);
 void *start_image,*exit_image; STATUS(EFIAPI *unload_image)(HANDLE);
 void *exit_boot_services,*get_monotonic,*stall,*set_watchdog,*connect_controller,*disconnect_controller;
 void *open_protocol,*close_protocol,*open_protocol_info,*protocols_per_handle,*locate_handle_buffer;
 STATUS(EFIAPI *locate_protocol)(GUID*,void*,void**);
} BS;
typedef struct {
 HEADER hdr;CHAR16 *vendor;uint32_t revision;HANDLE con_in_handle;void *con_in;
 HANDLE con_out_handle;void *con_out;HANDLE stderr_handle;void *stderr;
 void *runtime;BS *bs;
} SYSTEM;
_Static_assert(offsetof(SYSTEM,bs)==96,"system ABI");
_Static_assert(offsetof(BS,handle_protocol)==152,"BS protocol ABI");
_Static_assert(offsetof(BS,load_image)==200,"BS load ABI");
_Static_assert(offsetof(BS,unload_image)==224,"BS unload ABI");
_Static_assert(offsetof(BS,locate_protocol)==320,"BS locate ABI");
_Static_assert(offsetof(LOADED,base)==64,"loaded ABI");
static GUID loaded_guid={0x5b1b31a1,0x9562,0x11d2,{0x8e,0x3f,0,0xa0,0xc9,0x69,0x72,0x3b}};
static GUID fs_guid={0x964e5b22,0x6459,0x11d2,{0x8e,0x39,0,0xa0,0xc9,0x69,0x72,0x3b}};
typedef struct {uint32_t revision;void *reset,*set_attributes,*set_control,*get_control;
 STATUS(EFIAPI *write)(void*,UINTN*,void*);} SERIAL;
static SERIAL *uart;
static GUID serial_guid={0xbb25cf6f,0xf1d4,0x11d2,{0x9a,0x0c,0x00,0x90,0x27,0x3f,0xc1,0xfd}};
static void serial(const char *s) {
 UINTN n=0;while(s[n])n++;while(n){UINTN written=n;STATUS st=uart->write(uart,&written,(void*)s);if(ERROR(st)||!written||written>n)return;s+=written;n-=written;}
}
static void hex(U64 value) {char s[19];unsigned i;s[0]='0';s[1]='x';for(i=0;i<16;i++)s[2+i]="0123456789abcdef"[(value>>((15-i)*4))&15];s[18]=0;serial(s);}
static CHAR16 result_path[]={'N','T','R','E','S','U','L','T','.','E','F','I',0};
static CHAR16 expect_path[]={'E','X','P','E','C','T','.','B','I','N',0};
/* Seek-to-end sizing avoids allocating unbounded EFI_FILE_INFO metadata.
 * EFI GetPosition is required for this protocol; read-only handles only. */
static STATUS read_file(BS *bs,FILE *root,CHAR16 *path,UINTN limit,void **data,UINTN *size,unsigned *cleanup_bad) {
 FILE *f=0;STATUS st;UINTN n;void *p=0;
 typedef STATUS(EFIAPI *GET_POS)(FILE*,U64*);U64 file_size=0;
 *data=0;*size=0;
 st=root->open(root,&f,path,1,0);if(ERROR(st))return st;
 if(!f)return EFI_ERROR(7);
 st=f->set_position(f,UINT64_MAX);if(ERROR(st))goto finish;
 st=((GET_POS)f->get_position)(f,&file_size);if(ERROR(st))goto finish;
 if(!file_size || file_size>limit){st=EFI_ERROR(4);goto finish;}
 st=f->set_position(f,0);if(ERROR(st))goto finish;
 st=bs->allocate_pool(2,(UINTN)file_size,&p);if(ERROR(st))goto finish;
 if(!p){st=EFI_ERROR(9);goto finish;}
 n=(UINTN)file_size;st=f->read(f,&n,p);
 if(!ERROR(st) && n!=file_size)st=EFI_ERROR(7);
 if(!ERROR(st)){*data=p;*size=n;p=0;}
finish:
 if(p && ERROR(bs->free_pool(p)))*cleanup_bad=1;
 if(ERROR(f->close(f)))*cleanup_bad=1;
 return st;
}
STATUS EFIAPI efi_main(HANDLE self,SYSTEM *sys) {
 BS *bs=sys->bs;LOADED *own=0,*loaded=0;FS *fs=0;FILE *root=0;
 HANDLE child=0;void *image=0,*expect=0;UINTN image_size=0,expect_size=0;
 STATUS st=0;unsigned error=0,cleanup_bad=0;uint32_t entry=0;U64 want=0,got=0;
 st=bs->locate_protocol(&serial_guid,0,(void**)&uart);if(ERROR(st)||!uart)return EFI_ERROR(3);
 serial("BOOTSTRAP_C_TEST NTASM_RESULT_CONSUMER\r\n");
 st=bs->handle_protocol(self,&loaded_guid,(void**)&own);if(ERROR(st)||!own){error=1;goto finish;}
 st=bs->handle_protocol(own->device,&fs_guid,(void**)&fs);if(ERROR(st)||!fs){error=2;goto finish;}
 st=fs->open_volume(fs,&root);if(ERROR(st)||!root){error=3;goto finish;}
 st=read_file(bs,root,expect_path,8,&expect,&expect_size,&cleanup_bad);
 if(ERROR(st)||expect_size!=8){error=10;goto finish;}
 for(unsigned i=0;i<8;i++)want|=(U64)((uint8_t*)expect)[i]<<(i*8);
 st=read_file(bs,root,result_path,16*1024*1024,&image,&image_size,&cleanup_bad);
 if(ERROR(st)){error=11;goto finish;}
 error=(unsigned)ng_pe_validate(image,(size_t)image_size,0,&entry);
 if(error){error+=20;goto finish;}
 st=bs->load_image(0,self,0,image,image_size,&child);
 /* A SECURITY_VIOLATION may still yield a loaded image handle: retain ownership. */
 if(ERROR(st)||!child){error=30;goto finish;}
 st=bs->handle_protocol(child,&loaded_guid,(void**)&loaded);
 if(ERROR(st)||!loaded||!loaded->base||loaded->size>64*1024*1024){error=31;goto finish;}
 error=(unsigned)ng_pe_validate(loaded->base,(size_t)loaded->size,1,&entry);
 if(error){error+=40;goto finish;}
 serial("NG_NTASM_C_LOADED base=");hex((U64)(uintptr_t)loaded->base);serial(" size=");hex(loaded->size);serial(" entry=");hex(entry);serial("\r\n");
 got=((U64(EFIAPI *)(void))((uint8_t*)loaded->base+entry))();
 serial("NG_NTASM_C_RETURN got=");hex(got);serial(" expected=");hex(want);serial("\r\n");
 if(got!=want)error=50;
finish:
 if(child){if(ERROR(bs->unload_image(child)))cleanup_bad=1;child=0;}
 if(image){if(ERROR(bs->free_pool(image)))cleanup_bad=1;image=0;}
 if(expect){if(ERROR(bs->free_pool(expect)))cleanup_bad=1;expect=0;}
 if(root){if(ERROR(root->close(root)))cleanup_bad=1;root=0;}
 if(cleanup_bad)error=60;
 if(error){serial("NG_NTASM_C_RESULT_BAD code=");hex(error);serial(" status=");hex(st);serial("\r\n");return EFI_ERROR(1);}
 serial("NG_NTASM_C_RESULT_OK\r\n");return 0;
}
