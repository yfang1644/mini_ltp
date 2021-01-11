/*
 * ============================================================================
 *
 *       Filename:  sdnv.h
 *
 *    Description:  
 *
 *        Version:  1.0
 *        Created:  01/09/2021 04:33:42 PM
 *       Revision:  none
 *       Compiler: 
 *
 *         Author:  Fang Yuan (yfang@nju.edu.cn)
 *   Organization:  nju
 *
 * ============================================================================
 */

#ifndef _SDNV_H
#define _SDNV_H

void encodeSdnv(unsigned char *, unsigned int);
unsigned int decodeSdnv(unsigned char *);

#endif
