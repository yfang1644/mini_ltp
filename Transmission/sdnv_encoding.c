/*
 * ============================================================================
 *
 *       Filename:  sdnv_encoding.c
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

int sdnv(unsigned int val)
{
    unsigned char buf[8];   /* limited to maximum 8 bytes */
    unsigned char t;
    unsigned int result = 0;
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

    for (j = 0; j <= i; j++)
    {
        result |= buf[j] << (j*8);
    }

    result &= ~0x80;    /* MSB of the last byte should be 0 */

    return result;
}
