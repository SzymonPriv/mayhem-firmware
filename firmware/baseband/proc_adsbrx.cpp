/*
 * Copyright (C) 2014 Jared Boone, ShareBrained Technology, Inc.
 * Copyright (C) 2017 Furrtek
 *
 * This file is part of PortaPack.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street,
 * Boston, MA 02110-1301, USA.
 */

#include "proc_adsbrx.hpp"
#include "portapack_shared_memory.hpp"
#include "sine_table_int8.hpp"
#include "event_m4.hpp"

#include <cstdint>
#include <cstddef>

using namespace adsb;
	
void ADSBRXProcessor::execute(const buffer_c8_t& buffer) {
	int8_t re, im;
	float mag;
	uint32_t c;
	uint8_t bit, byte{};
	
	// This is called at 2M/2048 = 977Hz
	// One pulse = 500ns = 2 samples
	// One bit = 2 pulses = 1us = 4 samples

	if (!configured) return;
	
	for (size_t i = 0; i < buffer.count; i++) {
		
		// Compute sample's magnitude
		re = buffer.p[i].real(); // make re float and scale it
		im = buffer.p[i].imag(); // make re float and scale it
		mag = ((re * re) + (im * im)) * (k*k); 

		if (decoding) {
			// Decode
			
			// 1 bit lasts 2 samples
			if (sample_count & 1) {
				if (bit_count >= 112)
				{
					const ADSBFrameMessage message(frame);
					shared_memory.application_queue.push(message);
					decoding = false;

					if (prev_mag > mag)
						bit = 1;
					else
						bit = 0;

				}
				else 
				{
					//confidence = true;
					if (prev_mag > mag)
						bit = 1;
					else
						bit = 0;
				}
				
				byte = bit | (byte << 1);
				bit_count++;
				if (!(bit_count & 7)) {
					// Got one byte
					frame.push_byte(byte);
				}
			} // Second sample of each bit
			sample_count++;
		} else {
			// Look for preamble
			
			// Shift
			// FIXSBT make this a ring buffer
			// FIXSBT store level in int16 for quick compare to preamble
			for (c = 0; c < (ADSB_PREAMBLE_LENGTH - 1); c++)
			{
				shifter[c] = shifter[c + 1];
			}
			shifter[15] = mag;
			
			// First check of relations between the first 10 samples
			// representing a valid preamble. We don't even investigate further
			// if this simple test is not passed
			if (shifter[0] > shifter[1] &&
				shifter[1] < shifter[2] &&
				shifter[2] > shifter[3] &&
				shifter[3] < shifter[0] &&
				shifter[4] < shifter[0] &&
				shifter[5] < shifter[0] &&
				shifter[6] < shifter[0] &&
				shifter[7] > shifter[8] &&
				shifter[8] < shifter[9] &&
				shifter[9] > shifter[6])
			{
				// The samples between the two spikes must be < than the average
				// of the high spikes level. We don't test bits too near to
				// the high levels as signals can be out of phase so part of the
				// energy can be in the near samples
				float high = (shifter[0] + shifter[2] + shifter[7] + shifter[9]) / 12;
				if (shifter[4] < high &&
					shifter[5] < high)
				{

					// Similarly samples in the range 11-14 must be low, as it is the
					// space between the preamble and real data. Again we don't test
					// bits too near to high levels, see above
					if (shifter[11] < high &&
						shifter[12] < high &&
						shifter[13] < high &&
						shifter[14] < high)
					{
						//if (c == ADSB_PREAMBLE_LENGTH) {
						decoding = true;
						sample_count = 0;
						bit_count = 0;
						frame.clear();

						// Compute preamble pulses power to set thresholds
						//threshold = (shifter[0] + shifter[2] + shifter[7] + shifter[9]) / 4;
						// FIXSBT other use max * 0.2
						// FIXSBT threshold_high and threshold_low should be ditched 
						//threshold_high = threshold * 1.414f;		// +3dB
						//threshold_low = threshold * 0.707f;		// -3dB
					} // 11-14 low
				} // 4 & 5 high
			} // Check for preamble pattern
		}
		
		prev_mag = mag;
	}
}

void ADSBRXProcessor::on_message(const Message* const message) {
	if (message->id == Message::ID::ADSBConfigure) {
		bit_count = 0;
		sample_count = 0;
		decoding = false;
		configured = true;
	}
}

#ifndef _WIN32
int main() {
	EventDispatcher event_dispatcher { std::make_unique<ADSBRXProcessor>() };
	event_dispatcher.run();
	return 0;
}
#endif
