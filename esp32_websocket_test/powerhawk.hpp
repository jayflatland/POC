#pragma once

#include <WiFiUdp.h>

namespace powerhawk
{

    class powerhawk_type
    {
    public:
        using timestamp_t = unsigned long;

        timestamp_t period_start_us = 0;
        timestamp_t period_end_us = 0;
        timestamp_t last_report_us = 0;
        timestamp_t last_tx_us = 0;

        int ref_pin = 36;
        int in1_pin = 39;
        int in2_pin = 34;
        int in3_pin = 35;
        int in4_pin = 32;

        // we want a period to be an even multiple of 60Hz
        // 1/60Hz = 16.7ms
        // at 512 samples were 60Hz period:
        // 16.7ms / 512 = 65us
        // 1024 samples gives us 2 periods of 60Hz
        timestamp_t loop_us = 65;
        static const int HIST_CNT = 512;

        int hist_idx = 0;

        long ref_hist[HIST_CNT];
        long in1_hist[HIST_CNT];
        long in2_hist[HIST_CNT];
        long in3_hist[HIST_CNT];
        long in4_hist[HIST_CNT];

        // long in1_sq_hist[HIST_CNT];
        // long in2_sq_hist[HIST_CNT];
        // long in3_sq_hist[HIST_CNT];
        // long in4_sq_hist[HIST_CNT];

        // long in1_sq_sum = 0;
        // long in2_sq_sum = 0;
        // long in3_sq_sum = 0;
        // long in4_sq_sum = 0;

        // WiFiUDP Udp;

        void setup()
        {
            for (int i = 0; i < HIST_CNT; i++)
            {
                ref_hist[i] = 0;
                in1_hist[i] = 0;
                in2_hist[i] = 0;
                in3_hist[i] = 0;
                in4_hist[i] = 0;
            }

            pinMode(ref_pin, INPUT);
            pinMode(in1_pin, INPUT);
            pinMode(in2_pin, INPUT);
            pinMode(in3_pin, INPUT);
            pinMode(in4_pin, INPUT);
        }

        bool report = false;

        void loop()
        {
            timestamp_t now = micros();
            report = false;

            // still in our measurement period, stop
            if (period_start_us < now && now < period_end_us)
            {
                return;
            }

            // update the measure period
            period_start_us = now;
            period_end_us = period_start_us + loop_us;

            int ref = analogRead(ref_pin);
            long in1 = (long)(analogRead(in1_pin) - ref);
            long in2 = (long)(analogRead(in2_pin) - ref);
            long in3 = (long)(analogRead(in3_pin) - ref);
            long in4 = (long)(analogRead(in4_pin) - ref);

            ref_hist[hist_idx] = ref;
            in1_hist[hist_idx] = in1;
            in2_hist[hist_idx] = in2;
            in3_hist[hist_idx] = in3;
            in4_hist[hist_idx] = in4;

            // long in1_sq = in1 * in1;
            // long in2_sq = in2 * in2;
            // long in3_sq = in3 * in3;
            // long in4_sq = in4 * in4;

            // in1_sq_sum += in1_sq - in1_sq_hist[hist_idx];
            // in2_sq_sum += in2_sq - in2_sq_hist[hist_idx];
            // in3_sq_sum += in3_sq - in3_sq_hist[hist_idx];
            // in4_sq_sum += in4_sq - in4_sq_hist[hist_idx];

            // in1_sq_hist[hist_idx] = in1_sq;
            // in2_sq_hist[hist_idx] = in2_sq;
            // in3_sq_hist[hist_idx] = in3_sq;
            // in4_sq_hist[hist_idx] = in4_sq;

            hist_idx = (hist_idx + 1) % HIST_CNT;
            if(hist_idx == 0)
            {
                report = true;
            }

            // float reading_to_amps = 1.0 / 15.0;
            // float in1_rms = in1_sq_sum > 0 ? sqrt((float)(in1_sq_sum / HIST_CNT)) * reading_to_amps : 0.0;
            // float in2_rms = in2_sq_sum > 0 ? sqrt((float)(in2_sq_sum / HIST_CNT)) * reading_to_amps : 0.0;
            // float in3_rms = in3_sq_sum > 0 ? sqrt((float)(in3_sq_sum / HIST_CNT)) * reading_to_amps : 0.0;
            // float in4_rms = in4_sq_sum > 0 ? sqrt((float)(in4_sq_sum / HIST_CNT)) * reading_to_amps : 0.0;

            ///////////////////////////////////////////////////////////////////////////
            // REPORTING
            ///////////////////////////////////////////////////////////////////////////
            // if (1)
            // { // raw heart signal diagnostics
            //     if ((long)(now - last_report_us) > 1000000)
            //     {
            //         last_report_us = now;
            //         Serial.print(in1_rms);
            //         Serial.print(",");
            //         Serial.print(in2_rms);
            //         Serial.print(",");
            //         Serial.print(in3_rms);
            //         Serial.print(",");
            //         Serial.print(in4_rms);
            //         Serial.println();
            //     }
            // }

            // if (1)
            // { // raw heart signal diagnostics
            //     if ((long)(now - last_tx_us) > 1000000)
            //     {
            //         last_tx_us = now;
            //         String msg = String(in1_rms) + "," +
            //                      String(in2_rms) + "," +
            //                      String(in3_rms) + "," +
            //                      String(in4_rms);

            //         // Serial.print(amps);Serial.println();
            //         // Udp.beginPacket("10.1.10.255", 10243);
            //         Udp.beginPacket("10.1.10.11", 10243);
            //         // unsigned char buf[] = "hi from jay...\r\n";
            //         // Udp.write(buf, sizeof(buf));
            //         Udp.write((const uint8_t *)msg.c_str(), msg.length());
            //         Udp.endPacket();
            //     }
            // }
        }
    };
}
