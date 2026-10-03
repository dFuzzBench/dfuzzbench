

#ifndef VPF_H_INCLUDED
#define VPF_H_INCLUDED

struct vpf_fh;

struct vpf_fh *VPF_Open(const char *path, mode_t mode, pid_t *pidptr);
int VPF_Read(const char *path, pid_t *);
void VPF_Write(const struct vpf_fh *pfh);
void VPF_Remove(struct vpf_fh *pfh);

#endif
