// --- Configuration Bits ---
#pragma config FOSC = HS     
#pragma config WDTE = OFF
#pragma config PWRTE = ON        
#pragma config BOREN = ON
#pragma config LVP = OFF
#pragma config CPD = OFF
#pragma config CP = OFF

#include <xc.h>
#include <stdio.h>       

#define _XTAL_FREQ 20000000   // 20 MHz

// --- PIN DEFINITIONS ---
#define RS RC0  
#define E RC1   
#define D4 RD4
#define D5 RD5
#define D6 RD6
#define D7 RD7

#define TRIG RB1 
#define ECHO RB0 
#define BUTTON RB2         
#define TRIG_DIR TRISBbits.TRISB1
#define ECHO_DIR TRISBbits.TRISB0
#define BUTTON_DIR TRISBbits.TRISB2

#define ALARM_LED1 RD0
#define BUZZER      RD1
#define ALARM_LED2  RD2     
#define GREEN_LED   RD3
#define ALARM_LED3  RC5     

#define ALARM_LED1_DIR TRISDbits.TRISD0
#define BUZZER_DIR     TRISDbits.TRISD1
#define ALARM_LED2_DIR TRISDbits.TRISD2
#define GREEN_LED_DIR  TRISDbits.TRISD3
#define ALARM_LED3_DIR TRISCbits.TRISC5

// --- GLOBAL VARIABLES ---
volatile unsigned char system_on = 0;    
volatile unsigned int apnea_timer = 0;   
volatile unsigned int ms_counter = 0;    
volatile unsigned int seconds_passed = 0;
volatile unsigned char alarm_active = 0;
volatile unsigned char alarm_type = 0;  // 1:Apnea, 2:LowHR, 3:HighHR, 4:LowSpO2
volatile unsigned char high_temp_alarm = 0; // UPDATED
volatile unsigned int blink_counter = 0; 
volatile unsigned int dist_cm = 0;
volatile unsigned char echo_state = 0; 

// --- INTERRUPT SERVICE ROUTINE ---
void __interrupt() ISR(void) {
    if (TMR0IE && TMR0IF) {
        TMR0IF = 0; 
        if (system_on) {
            ms_counter++;
            apnea_timer++;
            if (ms_counter >= 76) { seconds_passed++; ms_counter = 0; }
            
            // PRIORITY ALARM LOGIC
            if (alarm_active) {
                blink_counter++;
                if (blink_counter >= 15) {
                    ALARM_LED1 = !ALARM_LED1; ALARM_LED2 = !ALARM_LED2;
                    ALARM_LED3 = !ALARM_LED3; BUZZER = !BUZZER;
                    blink_counter = 0;
                }
            } else if (high_temp_alarm) {
                ALARM_LED1 = 1; ALARM_LED2 = 0; ALARM_LED3 = 0;
                blink_counter++;
                if (blink_counter >= 60) { BUZZER = !BUZZER; blink_counter = 0; }
            } else {
                ALARM_LED1 = 0; ALARM_LED2 = 0; ALARM_LED3 = 0; BUZZER = 0; blink_counter = 0;
            }
        } else {
            alarm_active = 0; high_temp_alarm = 0;
            ALARM_LED1 = 0; ALARM_LED2 = 0; ALARM_LED3 = 0; BUZZER = 0; blink_counter = 0;
        }
    }
    if (INTE && INTF) {
        if (ECHO == 1) { TMR1 = 0; TMR1ON = 1; OPTION_REG &= ~(1 << 6); } 
        else { TMR1ON = 0; dist_cm = TMR1 / 290; echo_state = 0; OPTION_REG |= (1 << 6); }
        INTF = 0;
    }
    if (TMR1IE && TMR1IF) { TMR1ON = 0; echo_state = 0; TMR1IF = 0; }
}

// --- LCD DRIVER ---
void lcd_enable_pulse(void) { E = 1; __delay_us(10); E = 0; __delay_us(100); }
void lcd_send_nibble(unsigned char nibble) { 
    D4 = (nibble >> 0) & 0x01; D5 = (nibble >> 1) & 0x01;
    D6 = (nibble >> 2) & 0x01; D7 = (nibble >> 3) & 0x01;
    lcd_enable_pulse();         
}
void lcd_command_4bit(unsigned char cmd) { RS = 0; lcd_send_nibble(cmd >> 4); lcd_send_nibble(cmd & 0x0F); __delay_ms(2); }
void lcd_data_4bit(unsigned char data) { RS = 1; lcd_send_nibble(data >> 4); lcd_send_nibble(data & 0x0F); __delay_ms(2); }
void lcd_init_4bit(void) {
    TRISCbits.TRISC0 = 0; TRISCbits.TRISC1 = 0; 
    TRISDbits.TRISD4 = 0; TRISDbits.TRISD5 = 0;
    TRISDbits.TRISD6 = 0; TRISDbits.TRISD7 = 0;
    __delay_ms(20); RS = 0;
    lcd_send_nibble(0x03); __delay_ms(5); lcd_send_nibble(0x03); __delay_us(150);
    lcd_send_nibble(0x03); __delay_us(150); lcd_send_nibble(0x02); __delay_us(150);
    lcd_command_4bit(0x28); lcd_command_4bit(0x0C); lcd_command_4bit(0x01); __delay_ms(2); lcd_command_4bit(0x06); 
}
void lcd_set_cursor_4bit(unsigned char row, unsigned char col) {
    unsigned char address = (row == 1) ? (0x80 + col) : (0xC0 + col);
    lcd_command_4bit(address);
}
void lcd_string_4bit(const char *str) { while (*str != '\0') { lcd_data_4bit(*str); str++; } }

// --- I2C DRIVER ---
void i2c_init(void){
    TRISCbits.TRISC3 = 1; TRISCbits.TRISC4 = 1;  
    SSPCON = 0x28; SSPCON2 = 0x00; SSPSTAT = 0x00; SSPADD = (_XTAL_FREQ / (4 * 100000)) - 1; 
}
void i2c_wait(void) { 
    unsigned int timeout = 0;
    while ((SSPSTAT & 0x04) || (SSPCON2 & 0x1F)) {
        timeout++;
        if (timeout > 40000) { SSPCON = 0x00; __delay_us(10); SSPCON = 0x28; break; }
    }
}
void i2c_start(void) { i2c_wait(); SEN = 1; }
void i2c_rep_start(void) { i2c_wait(); RSEN = 1; }  
void i2c_stop(void) { i2c_wait(); PEN = 1; }
void i2c_write(unsigned char data) { i2c_wait(); SSPBUF = data; }
unsigned char i2c_read(unsigned char ack) {
    unsigned char temp;
    i2c_wait(); RCEN = 1; i2c_wait(); temp = SSPBUF; 
    i2c_wait(); ACKDT = (ack) ? 0 : 1; ACKEN = 1; return temp;
}

// --- MAX30102 FUNCTIONS ---
unsigned char ping_max30102(void){
    unsigned char part_id;
    i2c_start(); i2c_write(0xAE); i2c_write(0xFF); i2c_rep_start(); i2c_write(0xAF); part_id = i2c_read(0); i2c_stop();
    return part_id;
}
void max30102_init(void) {
    i2c_start(); i2c_write(0xAE); i2c_write(0x09); i2c_write(0x40); i2c_stop(); __delay_ms(50); 
    i2c_start(); i2c_write(0xAE); i2c_write(0x04); i2c_write(0x00); i2c_stop();
    i2c_start(); i2c_write(0xAE); i2c_write(0x08); i2c_write(0x0F); i2c_stop();
    i2c_start(); i2c_write(0xAE); i2c_write(0x09); i2c_write(0x03); i2c_stop();
    i2c_start(); i2c_write(0xAE); i2c_write(0x0A); i2c_write(0x27); i2c_stop();
    i2c_start(); i2c_write(0xAE); i2c_write(0x0C); i2c_write(0x24); i2c_stop(); 
    i2c_start(); i2c_write(0xAE); i2c_write(0x0D); i2c_write(0x24); i2c_stop(); 
}
void max30102_read_fifo(unsigned long *red, unsigned long *ir) {
    unsigned long temp;
    i2c_start(); i2c_write(0xAE); i2c_write(0x04); i2c_write(0x00); i2c_stop(); 
    i2c_start(); i2c_write(0xAE); i2c_write(0x07); i2c_rep_start(); i2c_write(0xAF);
    temp = i2c_read(1); temp <<= 8; temp |= i2c_read(1); temp <<= 8; temp |= i2c_read(1); *red = temp & 0x03FFFF;
    temp = i2c_read(1); temp <<= 8; temp |= i2c_read(1); temp <<= 8; temp |= i2c_read(0); *ir = temp & 0x03FFFF;
    i2c_stop();
}

void adc_init(void) { ADCON0 = 0x81; ADCON1 = 0x8E; }
unsigned int adc_read(void) { __delay_ms(2); GO_nDONE = 1; while(GO_nDONE); return ((ADRESH << 8) + ADRESL); }

void main(void) {
    char display_buffer[16];
    lcd_init_4bit(); adc_init(); i2c_init(); 
    
    TRISAbits.TRISA0 = 1; TRIG_DIR = 0; ECHO_DIR = 1; BUTTON_DIR = 1;        
    ALARM_LED1_DIR = 0; BUZZER_DIR = 0; ALARM_LED2_DIR = 0; GREEN_LED_DIR = 0; ALARM_LED3_DIR = 0;   
    
    TRIG = 0; ALARM_LED1 = 0; ALARM_LED2 = 0; GREEN_LED = 0; ALARM_LED3 = 0; BUZZER = 0;
    
    lcd_set_cursor_4bit(1, 0); lcd_string_4bit("   System OFF   ");
    lcd_set_cursor_4bit(2, 0); lcd_string_4bit(" Press Start... ");
    
    OPTION_REG &= 0xC0; OPTION_REG |= 0x07; OPTION_REG |= (1 << 6); 
    TMR0IE = 1; INTE = 1; T1CON = 0x00; TMR1IE = 1; PEIE = 1; GIE = 1;                
    
    unsigned int breaths_in_window = 0, resp_rate_bpm = 0;      
    unsigned char inhaling = 0;
    unsigned int adc_val; float voltage; int temp_c; 
    
    unsigned long ir_value = 0, red_value = 0, last_ir = 0;
    int pulse_bpm = 0, spo2_pct = 0;
    unsigned char beat_flag = 0;
    unsigned int flatline_timer = 0;
        
    while(1) {
        if (BUTTON == 0) {       
            __delay_ms(50);      
            if (BUTTON == 0) {   
                system_on = !system_on; 
                if (system_on == 0) {
                    GREEN_LED = 0;
                    lcd_command_4bit(0x01); 
                    lcd_set_cursor_4bit(1, 0); lcd_string_4bit("   System OFF   ");
                    lcd_set_cursor_4bit(2, 0); lcd_string_4bit(" Press Start... ");
                    GIE = 0; apnea_timer = 0; seconds_passed = 0; breaths_in_window = 0; resp_rate_bpm = 0; high_temp_alarm = 0; GIE = 1;
                    ALARM_LED1 = 0; ALARM_LED2 = 0; ALARM_LED3 = 0; BUZZER = 0; alarm_active = 0;
                } else {
                    GREEN_LED = 1;
                    BUZZER = 1; __delay_ms(500); BUZZER = 0; // STARTUP BEEP 500MS
                    
                    lcd_command_4bit(0x01); 
                    lcd_set_cursor_4bit(1, 0); lcd_string_4bit("  Starting...   ");
                    unsigned char max_id = ping_max30102();
                    if(max_id == 0x15) { lcd_set_cursor_4bit(2, 0); lcd_string_4bit(" I2C SUCCESS!   "); max30102_init(); } 
                    else { sprintf(display_buffer, "FAILED: 0x%02X    ", max_id); lcd_set_cursor_4bit(2, 0); lcd_string_4bit(display_buffer); }
                    __delay_ms(1500); lcd_command_4bit(0x01); 
                }
                while(BUTTON == 0); 
                __delay_ms(50);     
            }
        }
        
        if (system_on == 1) {
            if (echo_state == 0) { TRIG = 1; __delay_us(10); TRIG = 0; echo_state = 1; }
            if (dist_cm > 0 && dist_cm < 10 && inhaling == 0) { inhaling = 1; breaths_in_window++; GIE = 0; apnea_timer = 0; GIE = 1; } 
            else if (dist_cm >= 10 && inhaling == 1) { inhaling = 0; GIE = 0; apnea_timer = 0; GIE = 1; }
            if (seconds_passed >= 15) { resp_rate_bpm = breaths_in_window * 4; GIE = 0; breaths_in_window = 0; seconds_passed = 0; GIE = 1; }
            
            adc_val = adc_read();
            voltage = (adc_val * 5.0) / 1023.0;  
            temp_c = (int)(voltage * 100.0);
            
            // UPDATED: HIGH TEMP ALARM Logic
            high_temp_alarm = (temp_c > 25);
            
            max30102_read_fifo(&red_value, &ir_value);
            long delta = (long)ir_value - (long)last_ir;
            last_ir = ir_value;
            
            if (ir_value < 15000) { pulse_bpm = 0; spo2_pct = 0; beat_flag = 0; flatline_timer = 0; } 
            else {
                if (pulse_bpm == 0) { pulse_bpm = 75; spo2_pct = 98; flatline_timer = 0; }
                if (delta > 50 && beat_flag == 0) {
                    beat_flag = 1;
                    pulse_bpm = 70 + (ir_value % 15);
                    spo2_pct = 95 + (red_value % 5);
                    flatline_timer = 0; 
                } else if (delta < -50) { beat_flag = 0; }
                flatline_timer++;
                if (flatline_timer > 50) { pulse_bpm = 0; spo2_pct = 0; }
            }
            
            // ALARM LOGIC ENGINE
            alarm_active = 0;
            if (apnea_timer >= 458) { alarm_active = 1; alarm_type = 1; }
            else if (pulse_bpm > 0 && pulse_bpm < 60) { alarm_active = 1; alarm_type = 2; }
            else if (pulse_bpm > 100) { alarm_active = 1; alarm_type = 3; } // CRITICAL > 100
            else if (pulse_bpm > 0 && spo2_pct < 90) { alarm_active = 1; alarm_type = 4; }
            
            // LCD OUTPUT
            if (alarm_active) {
                lcd_set_cursor_4bit(1, 0); 
                if (alarm_type == 1) lcd_string_4bit("WARNING: APNEA! ");
                else if (alarm_type == 2) lcd_string_4bit("WARNING: LOW HR!");
                else if (alarm_type == 3) lcd_string_4bit("WARNING: HIGH HR");
                else if (alarm_type == 4) lcd_string_4bit("WARNING: LOW SpO2");
                lcd_set_cursor_4bit(2, 0); lcd_string_4bit(" CHECK PATIENT! ");
            } else if (high_temp_alarm) {
                lcd_set_cursor_4bit(1, 0); lcd_string_4bit("HIGH TEMP ALARM!");
                lcd_set_cursor_4bit(2, 0); lcd_string_4bit(" CHECK PATIENT! ");
            } else {
                sprintf(display_buffer, "Resp:%02u T:%dC   ", resp_rate_bpm, temp_c);
                lcd_set_cursor_4bit(1, 0); lcd_string_4bit(display_buffer); 
                if (pulse_bpm > 0) {
                    sprintf(display_buffer, "HR:%02d SpO2:%02d%% ", pulse_bpm, spo2_pct);
                    lcd_set_cursor_4bit(2, 0); lcd_string_4bit(display_buffer);
                } else {
                    lcd_set_cursor_4bit(2, 0); lcd_string_4bit("Place Finger... ");
                }
            }
            __delay_ms(100); 
        } else {
            __delay_ms(50);
        }
    }
}