/*
 * ============================================================================
 *
 *       Filename:  sdnv.c
 *
 *    Description:  Self-Delimiting Numeric Value (SDNV)
 *
 *        Version:  1.0
 *        Created:  01/08/2021 02:54:59 PM
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  Fang Yuan (yfang@nju.edu.cn)
 *   Organization:  nju
 *
 * ============================================================================
 */

void encodeSdnv(unsigned char *result, unsigned int val)
{
    unsigned char buf[8];   /* limited to maximum 8 bytes */
    unsigned char t;
    int i, j;

    for (i = 0; i < 8; i++)
    {
        t = val & 0x7f;     /* split a integer into 7-bit seg. */
        buf[i] = t | 0x80;  /* mark MSB 1 as default */
        val >>= 7;
        if (val == 0)
        {
            break;
        }
    }

    for (j = i; j > 0; j--)
    {
        *result++ = buf[j];
    }

    *result = (buf[j] & 0x7f);  /* MSB of the last byte should be 0 */
}

unsigned int decodeSdnv(unsigned char *sdnv)
{
    unsigned int val = 0;
    int i;
    unsigned char t;

    for(i = 0; i < 5; i++)
    {
        val <<= 7;
        t = *sdnv++;
        val |= t & 0x7f;
        t &= 0x80;
        if (t == 0)
        {
            break;
        }
    }

    return val;
}
