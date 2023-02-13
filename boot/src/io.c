/*
** Copyright (C) 2022-2023 Arseny Vakhrushev <arseny.vakhrushev@me.com>
**
** This firmware is free software: you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation, either version 3 of the License, or
** (at your option) any later version.
**
** This firmware is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this firmware. If not, see <http://www.gnu.org/licenses/>.
*/

#include "common.h"

#ifdef USARTv1
#define USART2_TDR USART2_DR
#define USART2_RDR USART2_DR
#define USART2_ISR USART2_SR
#define USART2_ICR USART2_SR
#define USART_ISR_RXNE USART_SR_RXNE
#define USART_ISR_TXE USART_SR_TXE
#define USART_ISR_TC USART_SR_TC
#define USART_ISR_FE USART_SR_FE
#define USART_ICR_FECF ~USART_SR_FE
#endif

void initio(void) {
#ifdef IO_PA2
	USART2_BRR = CLK_CNT(38400);
	USART2_CR3 = USART_CR3_HDSEL;
	USART2_CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE;
#else
	TIM3_ARR = CLK_CNT(38400) - 1; // Bit time
	TIM3_CCR1 = CLK_CNT(76800); // Half-bit time
	TIM3_EGR = TIM_EGR_UG;
	TIM3_CR1 = TIM_CR1_CEN;
	TIM3_SMCR = TIM_SMCR_SMS_RM | TIM_SMCR_TS_TI1F_ED; // Reset on any edge on TI1
	TIM3_CCMR1 = TIM_CCMR1_OC1PE | TIM_CCMR1_OC1M_PWM2 | TIM_CCMR1_CC2S_IN_TI1 | TIM_CCMR1_IC2F_CK_INT_N_8;
	TIM3_CCER = TIM_CCER_CC2E | TIM_CCER_CC2P; // IC2 on falling edge on TI1
#endif
	TIM14_PSC = CLK / 10000 - 1; // 0.1ms resolution
	TIM14_ARR = 4999; // 500ms I/O timeout
	TIM14_CR1 = TIM_CR1_URS;
	TIM14_EGR = TIM_EGR_UG;
	TIM14_CR1 = TIM_CR1_CEN | TIM_CR1_URS;
}

#ifdef IO_PA2
int recv(char *buf, int len) {
	TIM14_EGR = TIM_EGR_UG;
	TIM14_SR = ~TIM_SR_UIF;
	USART2_ICR = USART_ICR_FECF;
	for (int i = 0; i < len; ++i) {
		while (!(USART2_ISR & USART_ISR_RXNE)) {
			if (TIM14_SR & TIM_SR_UIF) return 0; // Timeout
		}
		buf[i] = USART2_RDR; // Clear RXNE
		if (USART2_ISR & USART_ISR_FE) return 0; // Data error
		TIM14_EGR = TIM_EGR_UG;
	}
	return 1;
}

void send(const char *buf, int len) {
	USART2_CR1 = USART_CR1_UE | USART_CR1_TE;
	for (int i = 0; i < len; ++i) {
		while (!(USART2_ISR & USART_ISR_TXE));
		USART2_TDR = buf[i]; // Clear TXE+TC
	}
	while (!(USART2_ISR & USART_ISR_TC));
	USART2_CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE;
}
#else
int recv(char *buf, int len) {
	TIM14_EGR = TIM_EGR_UG;
	TIM14_SR = ~TIM_SR_UIF;
	int n = 10, b = 0;
	for (;;) {
		int sr = TIM3_SR;
		if (sr & TIM_SR_CC1IF) { // Half-bit time
			TIM3_SR = ~TIM_SR_CC1IF;
			if (n == 10) continue;
			int p = TIM3_IDR; // Signal level
			if (!n++) { // Start bit
				if (p) return 0; // Data error
				b = 0;
				continue;
			}
			if (n < 10) { // Data bit
				b >>= 1;
				if (p) b |= 0x80;
				continue;
			}
			if (!p) return 0; // Data error
			*buf++ = b;
			if (!--len) return 1;
			TIM14_EGR = TIM_EGR_UG;
		}
		if (sr & TIM_SR_CC2IF) { // Falling edge
			TIM3_SR = ~TIM_SR_CC2IF;
			if (n == 10) n = 0;
		}
		if (TIM14_SR & TIM_SR_UIF) return 0; // Timeout
	}
}

void send(const char *buf, int len) {
	TIM3_SMCR = 0;
	TIM3_CCR1 = 0; // Preload high level
	TIM3_EGR = TIM_EGR_UG; // Update registers and trigger UEV
	TIM3_CCER = TIM_CCER_CC1E; // Enable output
	int n = 0, b = 0;
	for (;;) {
		if (!(TIM3_SR & TIM_SR_UIF)) continue;
		TIM3_SR = ~TIM_SR_UIF;
		if (!len) break;
		int p = -1;
		if (!n++) b = *buf++; // Start bit
		else if (n < 10) { // Data bit
			if (b & 1) p = 0;
			b >>= 1;
		} else { // Stop bit
			n = 0;
			p = 0;
			--len;
		}
		TIM3_CCR1 = p;
	}
	TIM3_SMCR = TIM_SMCR_SMS_RM | TIM_SMCR_TS_TI1F_ED; // Reset on any edge on TI1
	TIM3_CCER = TIM_CCER_CC2E | TIM_CCER_CC2P; // IC2 on falling edge on TI1
	TIM3_CCR1 = CLK_CNT(76800); // Half-bit time
	TIM3_EGR = TIM_EGR_UG;
}
#endif

int recvval(void) {
	char buf[2];
	return recv(buf, 2) && (buf[0] ^ buf[1]) == 0xff ? buf[0] : -1;
}

void sendval(int val) {
	char buf[2] = {val, ~val};
	send(buf, 2);
}

int recvdata(char *buf) {
	int cnt = recvval();
	if (cnt == -1) return -1;
	int len = (cnt + 1) << 2;
	uint32_t crc;
	return recv(buf, len) && recv((char *)&crc, 4) && crc32(buf, len) == crc ? len : -1;
}

void senddata(const char *buf, int len) {
	uint32_t crc = crc32(buf, len);
	sendval((len >> 2) - 1);
	send(buf, len);
	send((char *)&crc, 4);
}
