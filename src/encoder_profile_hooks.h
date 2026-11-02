#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct { uint32_t start, child; int id, bucket; } reflector_profile_scope;
reflector_profile_scope reflector_profile_enter(int id);
void reflector_profile_leave(reflector_profile_scope *scope);
#ifdef __cplusplus
}
#endif
