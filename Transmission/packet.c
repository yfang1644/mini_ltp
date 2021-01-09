/*
 * ============================================================================
 *
 *       Filename:  packet.c
 *
 *    Description:  
 *
 *        Version:  1.0
 *        Created:  01/09/2021 03:09:19 PM
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  Fang Yuan (yfang@nju.edu.cn)
 *   Organization:  nju
 *
 * ============================================================================
 */

#include "ltp.h"
#include <stdlib.h>
#include <unistd.h>

void packet(void)
{
    Sms_header p;

    p.version = 0;
    p.type = sdnv(4);   /* green data, not EOB */
    p.sessionId = sdnv((getpid() | (0x88<<16)));   /* ??? */
    p.hecnt = 1;
    p.tecnt = 0;
    p.ext = malloc(sizeof(Sms_ext)*2);
    p.ext->tag = 0;
    p.ext->length = malloc(4);
    p.ext->value = malloc(256);
}

