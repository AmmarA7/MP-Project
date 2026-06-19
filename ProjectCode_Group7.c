// Configuration bits
#pragma config FOSC = HS     // High speed oscillator required for the 20MHz crystal
#pragma config WDTE = OFF    // Watchdog timer disabled so the chip does not randomly reset itself
#pragma config PWRTE = ON    // Power-up timer enabled to let voltage stabilize on boot
#pragma config BOREN = ON    // Brown-out reset enabled to safely reset if power drops suddenly
#pragma config LVP = OFF     // Low voltage programming disabled to free up pin RB3
#pragma config CPD = OFF     // Data EEPROM memory code protection off
#pragma config CP = OFF      // Flash program memory code protection off
 
#include <xc.h>
#include <stdio.h>        
 
#define _XTAL_FREQ 20000000  // 20MHz to reduce response time 
 
// Pin definitions
// LCD display pins in 4-bit mode to save wiring space
#define RS RC0  
#define E RC1   
#define D4 RD4
#define D5 RD5
#define D6 RD6
#define D7 RD7
 
// Ultrasonic sensor pins for measuring chest expansion during breathing 
#define TRIG RB1 
#define ECHO RB0 
#define BUTTON RB2 // Override button to turn the system on or standby
#define TRIG_DIR TRISBbits.TRISB1
#define ECHO_DIR TRISBbits.TRISB0
#define BUTTON_DIR TRISBbits.TRISB2
 
// Warning system pins
#define ALARM_LED1 RD0
#define BUZZER RD1
#define ALARM_LED2 RD2     
#define GREEN_LED RD3
#define ALARM_LED3 RC5     
 
// Direction mapping
#define ALARM_LED1_DIR TRISDbits.TRISD0
#define BUZZER_DIR     TRISDbits.TRISD1
#define ALARM_LED2_DIR TRISDbits.TRISD2
#define GREEN_LED_DIR  TRISDbits.TRISD3
#define ALARM_LED3_DIR TRISCbits.TRISC5
 
// Global variables
volatile unsigned char system_on = 0;  // System power state toggled by the nurse
volatile unsigned int apnea_timer = 0; // Tracks how long since the patient last took a breath
volatile unsigned int ms_counter = 0;  // General millisecond tracker for 1 second ticks
volatile unsigned int seconds_passed = 0; // Tracks the 15-second window for breaths per minute calculation
volatile unsigned char alarm_active = 0; // 0 means running correct, 1 means critical medical alarm triggered
volatile unsigned char alarm_type = 0;  // 1 is Apnea, 2 is LowHR, 3 is HighHR, 4 is LowSpO2
volatile unsigned char high_temp_alarm = 0; // separate flag specifically for high temperature warnings
volatile unsigned int blink_counter = 0;  // Controls how fast the LEDs flash during an emergency
volatile unsigned int dist_cm = 0;  // Calculated chest distance from the ultrasonic sensor
volatile unsigned char echo_state = 0;  // 0 is ready to ping, 1 is waiting for sound wave to return
volatile unsigned int refractory_timer = 0; // Hardware lockout to prevent false double-breaths from sensor noise
volatile unsigned char lcd_seconds = 0;  // Tracks 5-second intervals to smooth out the vital readings on the display
volatile unsigned int beat_interval_timer = 0; // Hardware stopwatch ticking every 13.1ms to measure exact time between beats
volatile unsigned char valid_beats = 0; // Counter to delay alarms until the heart rate reading stabilizes
unsigned long ir_avg = 0; // Dynamic baseline low-pass filter to adapt to different skin thicknesses
volatile unsigned char startup_beat = 1;  // Tracks the very first beat to eliminate mechanical finger-placement lag


// ISR
void __interrupt() ISR(void) {
    
    // Timer 0 is system heartbeat that overflows every 13.1ms at 20MHz
    if (TMR0IE && TMR0IF) {
        TMR0IF = 0;  // Clear the interrupt flag immediately so it does not get stuck in an infinite loop
        
        if (system_on) {  // Only track time if the system is actively monitoring
            ms_counter++;
            apnea_timer++;
            beat_interval_timer++; // Tick up every 13.1ms for mathematically precise peak-to-peak heart rate calculation
            
            // Tick down the physical breath lockout timer if it is currently active 
            if (refractory_timer > 0) {
                refractory_timer--;
            }
 
            // 76 overflows multiplied by 13.1ms is roughly 1.0 second elapsed
            if (ms_counter >= 76) { 
                seconds_passed++; 
                lcd_seconds++; // Advance the display timer safely in hardware
                ms_counter = 0; 
            }
            
            // Priority alarm system, Apnea and HR are more critical than temp
            if (alarm_active) {
                blink_counter++;
                if (blink_counter >= 15) { // Flash very fast for critical emergencies
                    ALARM_LED1 = !ALARM_LED1; ALARM_LED2 = !ALARM_LED2;
                    ALARM_LED3 = !ALARM_LED3; BUZZER = !BUZZER;
                    blink_counter = 0;
                }
            } else if (high_temp_alarm) {
                ALARM_LED1 = 1; ALARM_LED2 = 0; ALARM_LED3 = 0; // Solid red LED for high room temperature
                blink_counter++;
                if (blink_counter >= 60) { BUZZER = !BUZZER; blink_counter = 0; } // Slow warning beep for temperature
            } else {
                // Rigidly enforce that the warning hardware stays off if the patient is stable
                ALARM_LED1 = 0; ALARM_LED2 = 0; ALARM_LED3 = 0; BUZZER = 0; blink_counter = 0;
            }
        } else {
            // If system is forced off by the nurse, kill all physical alarms immediately
            alarm_active = 0; high_temp_alarm = 0;
            ALARM_LED1 = 0; ALARM_LED2 = 0; ALARM_LED3 = 0; BUZZER = 0; blink_counter = 0;
        }
    }
    
    // External interrupt on pin RB0 for ultrasonic echo processing 
    if (INTE && INTF) {
        if (ECHO == 1) { // Rising edge means the echo sound wave just left the sensor
            TMR1 = 0; 
            TMR1ON = 1; // Start Timer 1 to act as a precision microsecond stopwatch
            OPTION_REG &= ~(1 << 6); // Swap interrupt detection to look for the falling edge return now
        } 
        else {  // Falling edge means the echo sound wave just bounced back
            TMR1ON = 0; // Stop the stopwatch
            dist_cm = TMR1 / 290;  // Convert the raw hardware ticks into physical centimeters
            echo_state = 0;  // Reset state so the main loop can fire another ping
            OPTION_REG |= (1 << 6); // Swap interrupt detection back to rising edge for the next ping
        }
        INTF = 0; // Clear the interrupt flag
    }
    
    // Timer 1 ultrasonic timeout safety
    if (TMR1IE && TMR1IF) { // If the sound wave scatters and never returns this prevents system freezes
        TMR1ON = 0; echo_state = 0; TMR1IF = 0; 
    }
}
 

// LCD driver in 4-bit mode
void lcd_enable_pulse(void) { E = 1; __delay_us(10); E = 0; __delay_us(100); } // Blips the enable pin so the LCD catches the data
void lcd_send_nibble(unsigned char nibble) { 
    // Split the 4 bits across the specific data pins to save wiring on the physical board
    D4 = (nibble >> 0) & 0x01; D5 = (nibble >> 1) & 0x01;
    D6 = (nibble >> 2) & 0x01; D7 = (nibble >> 3) & 0x01;
    lcd_enable_pulse();         
}
void lcd_command_4bit(unsigned char cmd) { RS = 0; lcd_send_nibble(cmd >> 4); lcd_send_nibble(cmd & 0x0F); __delay_ms(2); } // RS at 0 means configuration command
void lcd_data_4bit(unsigned char data) { RS = 1; lcd_send_nibble(data >> 4); lcd_send_nibble(data & 0x0F); __delay_ms(2); } // RS at 1 means character to display
void lcd_init_4bit(void) {
    TRISCbits.TRISC0 = 0; TRISCbits.TRISC1 = 0; 
    TRISDbits.TRISD4 = 0; TRISDbits.TRISD5 = 0;
    TRISDbits.TRISD6 = 0; TRISDbits.TRISD7 = 0;
    __delay_ms(20); RS = 0;
    // Mandatory boot-up sequence required by the physical LCD controller chip
    lcd_send_nibble(0x03); __delay_ms(5); lcd_send_nibble(0x03); __delay_us(150);
    lcd_send_nibble(0x03); __delay_us(150); lcd_send_nibble(0x02); __delay_us(150);
    lcd_command_4bit(0x28); lcd_command_4bit(0x0C); lcd_command_4bit(0x01); __delay_ms(2); lcd_command_4bit(0x06); 
}
void lcd_set_cursor_4bit(unsigned char row, unsigned char col) {
    // Map the row to the physical memory addresses inside the LCD RAM
    unsigned char address = (row == 1) ? (0x80 + col) : (0xC0 + col);
    lcd_command_4bit(address);
}
void lcd_string_4bit(const char *str) { while (*str != '\0') { lcd_data_4bit(*str); str++; } } // Print string character by character
 

// I2C driver
void i2c_init(void){
    TRISCbits.TRISC3 = 1; TRISCbits.TRISC4 = 1;  // SCL and SDA must be inputs for the hardware module to control them natively
    SSPCON = 0x28; SSPCON2 = 0x00; SSPSTAT = 0x00; 
    SSPADD = (_XTAL_FREQ / (4 * 100000)) - 1; // Set I2C clock speed to standard 100kHz based on the crystal frequency
}
void i2c_wait(void) { 
    unsigned int timeout = 0;
    // Wait until the I2C bus is idle meaning no active transmissions are happening
    while ((SSPSTAT & 0x04) || (SSPCON2 & 0x1F)) {
        timeout++;
        if (timeout > 40000) { SSPCON = 0x00; __delay_us(10); SSPCON = 0x28; break; } // Safety reset if the physical bus freezes
    }
}
void i2c_start(void) { i2c_wait(); SEN = 1; } // Initiate start condition
void i2c_rep_start(void) { i2c_wait(); RSEN = 1; }  // Repeated start for reading data
void i2c_stop(void) { i2c_wait(); PEN = 1; } // Initiate stop condition
void i2c_write(unsigned char data) { i2c_wait(); SSPBUF = data; } // Push data into hardware buffer to send
unsigned char i2c_read(unsigned char ack) {
    unsigned char temp;
    i2c_wait(); RCEN = 1; i2c_wait(); temp = SSPBUF;  // Pull data from hardware buffer
    i2c_wait(); ACKDT = (ack) ? 0 : 1; ACKEN = 1; return temp; // Send acknowledge bit
}
 
// Pulse oximeter driver
unsigned char ping_max30102(void){
    unsigned char part_id;
    i2c_start(); i2c_write(0xAE); i2c_write(0xFF); i2c_rep_start(); i2c_write(0xAF); part_id = i2c_read(0); i2c_stop();
    return part_id; // Reads the factory part ID to verify the sensor is wired correctly before booting
}
void max30102_init(void) {
    // Configure the internal registers of the sensor to turn on the LEDs and setup the ADC
    i2c_start(); i2c_write(0xAE); i2c_write(0x09); i2c_write(0x40); i2c_stop(); 
    __delay_ms(50);  // Give the chip time to hard reset 
    i2c_start(); i2c_write(0xAE); i2c_write(0x04); i2c_write(0x00); i2c_stop(); 
    i2c_start(); i2c_write(0xAE); i2c_write(0x05); i2c_write(0x00); i2c_stop(); 
    i2c_start(); i2c_write(0xAE); i2c_write(0x06); i2c_write(0x00); i2c_stop(); 
    i2c_start(); i2c_write(0xAE); i2c_write(0x08); i2c_write(0x1F); i2c_stop(); 
    i2c_start(); i2c_write(0xAE); i2c_write(0x09); i2c_write(0x03); i2c_stop(); 
    i2c_start(); i2c_write(0xAE); i2c_write(0x0A); i2c_write(0x27); i2c_stop(); 
    i2c_start(); i2c_write(0xAE); i2c_write(0x0C); i2c_write(0x1F); i2c_stop(); 
    i2c_start(); i2c_write(0xAE); i2c_write(0x0D); i2c_write(0x1F); i2c_stop(); 
}

unsigned char max30102_read_fifo(unsigned long *red, unsigned long *ir) {
    unsigned char wr_ptr, rd_ptr;
    unsigned long temp;
    
    // Check the hardware pointers to see if new data has arrived yet
    i2c_start(); i2c_write(0xAE); i2c_write(0x04); i2c_rep_start(); i2c_write(0xAF); wr_ptr = i2c_read(0); i2c_stop();
    i2c_start(); i2c_write(0xAE); i2c_write(0x06); i2c_rep_start(); i2c_write(0xAF); rd_ptr = i2c_read(0); i2c_stop();
    
    if (wr_ptr == rd_ptr) return 0; // Buffer is empty so skip math to save processing cycles
    
    // Pull the raw 18-bit light readings from the FIFO data register
    i2c_start(); i2c_write(0xAE); i2c_write(0x07); i2c_rep_start(); i2c_write(0xAF);
    temp = i2c_read(1); temp <<= 8; temp |= i2c_read(1); temp <<= 8; temp |= i2c_read(1); *red = temp & 0x03FFFF;
    temp = i2c_read(1); temp <<= 8; temp |= i2c_read(1); temp <<= 8; temp |= i2c_read(0); *ir = temp & 0x03FFFF;
    i2c_stop();
    
    return 1; // Successfully read new data
}
 
// Temperature driver
void adc_init(void) { ADCON0 = 0x81; ADCON1 = 0x8E; } // Set ADC clock to required 20MHz stability and turn ADC on
unsigned int adc_read(void) { 
    __delay_ms(2); 
    GO_nDONE = 1;  // Tell the hardware to start converting analog voltage to a digital number 
    while(GO_nDONE);  // Wait here until the hardware finishes the conversion
    return ((ADRESH << 8) + ADRESL); // Combine the two 8 bit registers right justified for maximum precision
}


// Main programm
void main(void) {
    char display_buffer[16]; // Text array used to format variables into printable strings
    
    // Hardware initializations
    lcd_init_4bit(); adc_init(); i2c_init(); 
    
    // Pin direction configuration 
    TRISAbits.TRISA0 = 1; TRIG_DIR = 0; ECHO_DIR = 1; BUTTON_DIR = 1;        
    ALARM_LED1_DIR = 0; BUZZER_DIR = 0; ALARM_LED2_DIR = 0; GREEN_LED_DIR = 0; ALARM_LED3_DIR = 0;   
    
    // Forcefully turn off all warning indicators on initial boot
    TRIG = 0; ALARM_LED1 = 0; ALARM_LED2 = 0; GREEN_LED = 0; ALARM_LED3 = 0; BUZZER = 0;
    
    // Show default standby screen
    lcd_set_cursor_4bit(1, 0); lcd_string_4bit("   System OFF   ");
    lcd_set_cursor_4bit(2, 0); lcd_string_4bit(" Press Start... ");
    
    // Hardware interrupt configuration
    OPTION_REG &= 0xC0; OPTION_REG |= 0x07; // Set Timer0 prescaler to 1:256 to slow down the system heartbeat
    OPTION_REG |= (1 << 6); // Set external interrupt to trigger on the rising edge of the sound wave
    
    TMR0IE = 1; INTE = 1; T1CON = 0x00; TMR1IE = 1; PEIE = 1; GIE = 1; // Enable specific interrupts then flip the global switch
    
    // Respiration and temperature variables
    unsigned int breaths_in_window = 0, resp_rate_bpm = 0;      
    unsigned char inhaling = 0;
    unsigned int adc_val; float voltage; int temp_c; 
    
    // Core engine variables for fast updates and instant safety alarms
    unsigned long ir_value = 0, red_value = 0;
    int pulse_bpm = 0;
    unsigned char beat_flag = 0;
    unsigned int flatline_timer = 0;
    unsigned char finger_off_counter = 0; 
    unsigned char lcd_refresh_counter = 0; // Multiplexer counter to prevent screen smearing
    
    // Display engine variables using a slow accumulator to prevent LCD flickering
    int display_bpm = 0;
    unsigned int sum_bpm = 0;
    unsigned char beat_count = 0;
    
    while(1) { // Main infinite program loop
        
        // Button override logic
        if (BUTTON == 0) {       
            __delay_ms(50); // Mechanical debounce to prevent physical button bounce from registering multiple presses
            if (BUTTON == 0) {   
                system_on = !system_on; // Toggle the system monitoring flag
                
                if (system_on == 0) { // Transition to standby when nurse turns system off
                    GREEN_LED = 0;
                    lcd_command_4bit(0x01); 
                    lcd_set_cursor_4bit(1, 0); lcd_string_4bit("   System OFF   ");
                    lcd_set_cursor_4bit(2, 0); lcd_string_4bit(" Press Start... ");
                    
                    // Disable global interrupts temporarily to safely reset shared memory without the ISR interfering
                    GIE = 0; 
                    apnea_timer = 0; seconds_passed = 0; lcd_seconds = 0; beat_interval_timer = 0;
                    breaths_in_window = 0; resp_rate_bpm = 0; high_temp_alarm = 0; 
                    refractory_timer = 0; GIE = 1; // Enable interrupts again
                    
                    ALARM_LED1 = 0; ALARM_LED2 = 0; ALARM_LED3 = 0; BUZZER = 0; alarm_active = 0;
                    
                    // Reset display engine
                    display_bpm = 0; sum_bpm = 0; beat_count = 0; pulse_bpm = 0; ir_value = 0; valid_beats = 0; ir_avg = 0;
                    startup_beat = 1;
                } else { // Transition to active monitoring when nurse turns system on
                    GREEN_LED = 1;
                    BUZZER = 1; __delay_ms(2500); BUZZER = 0; // Extended startup beep
                    
                    lcd_command_4bit(0x01); 
                    lcd_set_cursor_4bit(1, 0); lcd_string_4bit("  Starting...   ");
                    unsigned char max_id = ping_max30102(); // Check if sensor is alive
                    if(max_id == 0x15) { lcd_set_cursor_4bit(2, 0); lcd_string_4bit(" I2C SUCCESS!   "); max30102_init(); } 
                    else { sprintf(display_buffer, "FAILED: 0x%02X    ", max_id); lcd_set_cursor_4bit(2, 0); lcd_string_4bit(display_buffer); }
                    __delay_ms(1500); lcd_command_4bit(0x01); 
                    
                    GIE = 0; lcd_seconds = 0; GIE = 1; // Sync display timer at start
                }
                while(BUTTON == 0); // Freeze the code here until the nurse lifts their finger off the button
                __delay_ms(50);  // Debounce release
            }
        }
        
        // Active patient monitoring
        if (system_on == 1) {
            
            // Respiration tracking
            if (echo_state == 0) { TRIG = 1; __delay_us(10); TRIG = 0; echo_state = 1; } // Send a tiny 10us pulse to fire the sound wave
            
            // Register an inhale if chest expands more than 10cm and the physical lockout is clear
            if (dist_cm > 0 && dist_cm < 10 && inhaling == 0 && refractory_timer == 0) { 
                inhaling = 1; breaths_in_window++; 
                GIE = 0; apnea_timer = 0; refractory_timer = 76; GIE = 1; // Set lockout to roughly 1 second to prevent double counting
            } else if (dist_cm >= 10 && inhaling == 1) { 
                inhaling = 0; GIE = 0; apnea_timer = 0; GIE = 1; // Register the exhale
            }
            
            // Every 15 seconds multiply the counted breaths by 4 to get a full 60 second BPM
            if (seconds_passed >= 15) { resp_rate_bpm = breaths_in_window * 4; GIE = 0; breaths_in_window = 0; seconds_passed = 0; GIE = 1; }
            
            // Temperature tracking
            adc_val = adc_read(); // Get raw 0 to 1023 value
            voltage = (adc_val * 5.0) / 1023.0;  // Find actual voltage
            temp_c = (int)(voltage * 100.0); // LM35 outputs 10mV per degree so multiply by 100 for exact Celsius
            high_temp_alarm = (temp_c > 26); // Trip the fever flag if temp exceeds threshold
            
            // Heart rate and SpO2 tracking
            if (max30102_read_fifo(&red_value, &ir_value)) {
                
                // Create a slow moving baseline to account for different skin tones and room lighting
                if (ir_avg == 0) ir_avg = ir_value; 
                ir_avg = ((ir_avg * 31) + ir_value) / 32; // Mathematical low pass filter
                
                // Subtract the baseline from the current reading to isolate the actual blood surge
                long delta = (long)ir_value - (long)ir_avg; 
                
                // detect if finger is completely removed
                if (ir_value < 30000) { 
                    finger_off_counter++;
                    if (finger_off_counter > 5) { 
                        pulse_bpm = 0; beat_flag = 0; flatline_timer = 0; beat_interval_timer = 0;
                        display_bpm = 0; sum_bpm = 0; beat_count = 0; valid_beats = 0; ir_avg = 0;
                        startup_beat = 1; // Reset startup lock when finger is removed
                    }
                } 
                else {
                    if (finger_off_counter > 0) {
                        beat_flag = 1; // Ignore the initial shadow artifact from placing the finger
                        startup_beat = 1; // Prepare to catch the very first clean beat
                    }
                    finger_off_counter = 0; 
                    
                    // Blood pulse detected when the light drops sharply indicating a surge of blood
                    if (delta > 50 && beat_flag == 0) {
                        beat_flag = 1;
                        
                        GIE = 0; 
                        unsigned int ticks = beat_interval_timer; // Capture the stopwatch value
                        beat_interval_timer = 0; // Restart stopwatch instantly for the next beat
                        GIE = 1;
                        
                        if (startup_beat == 1) {
                            // This is the first pulse without a previous pulse to measure against
                            // The stopwatch started perfectly on the rising edge of this beat
                            startup_beat = 0;
                        } 
                        else {
                            // This is the second pulse giving a mathematically perfect peak to peak measurement
                            if (ticks > 40 && ticks < 310) { // Ensure the beat is physically possible
                                int new_bpm = 9155 / ticks;  // Convert the hardware timer ticks directly into beats per minute
                                
                                if (pulse_bpm == 0) {
                                    pulse_bpm = new_bpm;
                                    display_bpm = new_bpm; // Instant LCD feedback on the first real math cycle
                                } else {
                                    pulse_bpm = ((pulse_bpm * 3) + new_bpm) / 4; // Weighted rolling average for safety engine
                                }
                                
                                sum_bpm += new_bpm;
                                beat_count++;
                                if (valid_beats < 10) valid_beats++; // Count up so alarms know the data is stable
                            }
                        }
                        
                        flatline_timer = 0; 
                    } else if (delta < -20 && beat_interval_timer > 35) { 
                        beat_flag = 0; // Reset flag on the down slope of the pulse ensuring enough time has passed
                    }
                    
                    flatline_timer++;
                    if (flatline_timer > 80) { // If no pulse in a long time drop values to 0
                        pulse_bpm = 0; display_bpm = 0; valid_beats = 0; startup_beat = 1;
                    }
                }
            }
            
            // The 5 second display engine
            // Averages beats over 5 seconds to prevent the LCD from flickering wildly
            if (lcd_seconds >= 5) {
                if (beat_count > 0) {
                    display_bpm = sum_bpm / beat_count;
                }
                sum_bpm = 0; beat_count = 0;
                GIE = 0; lcd_seconds = 0; GIE = 1; // Safely reset the hardware timer
            }
            
            // Priority alarm logic engine evaluating medical severity
            alarm_active = 0;
            // Check flags in order of severity with apnea being the most critical
            if (apnea_timer >= 458) { alarm_active = 1; alarm_type = 1; }
            else if (valid_beats > 2) { // Prevents false alarms while the sensor is still locking onto the pulse
                if (pulse_bpm > 0 && pulse_bpm < 60) { alarm_active = 1; alarm_type = 2; }
                else if (pulse_bpm > 100) { alarm_active = 1; alarm_type = 3; } 
            }
            
            // LCD output multiplexer
            lcd_refresh_counter++;
            if (lcd_refresh_counter >= 10) { // Only update the physical screen occasionally to prevent visual smearing
                
                if (alarm_active) {
                    // If a critical alarm is triggered aggressively override row 1 to display the specific emergency
                    lcd_set_cursor_4bit(1, 0); 
                    if (alarm_type == 1) lcd_string_4bit("WARNING: APNEA! ");
                    else if (alarm_type == 2) lcd_string_4bit("WARNING: LOW HR!");
                    else if (alarm_type == 3) lcd_string_4bit("WARNING: HIGH HR");
                    lcd_set_cursor_4bit(2, 0); lcd_string_4bit(" CHECK PATIENT! ");
                } else if (high_temp_alarm) {
                    lcd_set_cursor_4bit(1, 0); lcd_string_4bit("HIGH TEMP ALARM!");
                    lcd_set_cursor_4bit(2, 0); lcd_string_4bit(" CHECK PATIENT! ");
                } else {
                    // Everything is stable so print normal vitals using sprintf to inject the calculated variables
                    sprintf(display_buffer, "Resp:%02u T:%dC   ", resp_rate_bpm, temp_c);
                    lcd_set_cursor_4bit(1, 0); lcd_string_4bit(display_buffer); 
                    
                    if (display_bpm > 0) { 
                        sprintf(display_buffer, "HR:%03d BPM      ", display_bpm);
                        lcd_set_cursor_4bit(2, 0); lcd_string_4bit(display_buffer);
                    } else if (ir_value >= 30000) {
                        // Keep the user informed about exactly what the system is doing while it gathers data
                        if (startup_beat == 0) {
                            lcd_set_cursor_4bit(2, 0); lcd_string_4bit("Locking Pulse...");
                        } else {
                            lcd_set_cursor_4bit(2, 0); lcd_string_4bit("Measuring HR... ");
                        }
                    } else {
                        lcd_set_cursor_4bit(2, 0); lcd_string_4bit("Place Finger... ");
                    }
                }
                lcd_refresh_counter = 0; // Reset the display multiplexer
            }
            
            __delay_ms(10);  // Standard tiny delay to keep the main loop from spinning out of control
        } else {
            // System is in standby so just idle briefly to save processing cycles
            __delay_ms(50);
        }
    }
}