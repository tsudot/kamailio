#ifndef UAC_API_H_
#define UAC_API_H_
#include "../../sr_module.h"


typedef int (*uac_replace_from_t)(sip_msg_t *, str *, str *);

typedef struct uac_binds {
	uac_replace_from_t	replace_from;
} uac_api_t;

typedef int (*bind_uac_f)(uac_api_t*);

int bind_uac(uac_api_t*);

inline static int load_uac_api(uac_api_t *uacb){
	bind_uac_f bind_uac_exports;
	if(!(bind_uac_exports=(bind_uac_f)find_export("bind_uac",1,0))){
		LM_ERR("Failed to import bind_iuax\n");
		return -1;
	}
	return bind_uac_exports(uacb);
}

#endif /* UAC_API_H_ */
