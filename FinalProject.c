// Configuration Bits
#pragma config FOSC = XT
#pragma config WDTE = OFF
#pragma config PWRTE = ON        
#pragma config BOREN = ON
#pragma config LVP = OFF
#pragma config CPD = OFF
#pragma config CP = OFF

#include <xc.h>
#include <stdio.h>      

#define _XTAL_FREQ 4000000

// --- PIN DEFINITIONS ---
#define RS RC0
#define E RC1   
#define D4 RD4
#define D5 RD5
#define D6 RD6
#define D7 RD7

// HC-SR04 Pins (SWAPPED FOR HARDWARE INTERRUPT!)
#define TRIG RB1 // Changed to RB1
#define ECHO RB0 // MUST be RB0 for External Interrupt (INT)
#define TRIG_DIR TRISBbits.TRISB1
#define ECHO_DIR TRISBbits.TRISB0

// Alarm Pins
#define ALARM_LED RD0
#define BUZZER RD1
#define ALARM_LED_DIR TRISDbits.TRISD0
#define BUZZER_DIR TRISDbits.TRISD1

// --- GLOBAL VARIABLES ---
// ISR Watchdog variables
volatile unsigned int apnea_timer = 0;   
volatile unsigned int ms_counter = 0;    
volatile unsigned int seconds_passed = 0;
volatile unsigned char alarm_active = 0; 

// Ultrasonic Interrupt variables
volatile unsigned int dist_cm = 0;
volatile unsigned char echo_state = 0; // 0 = Waiting for Rising Edge, 1 = Waiting for Falling Edge

// --- INTERRUPT SERVICE ROUTINE (ISR) ---
void __interrupt() ISR(void) {
    
    // 1. TIMER0 INTERRUPT: Apnea Watchdog (Ticks every 65.5ms)
    if (TMR0IE && TMR0IF) {
        TMR0IF = 0; 
        
        ms_counter++;
        apnea_timer++;
        
        if (ms_counter >= 15) {
            ms_counter = 0;
            seconds_passed++;
        }

        if (apnea_timer > 90) { // ~6 seconds without a breath
            alarm_active = 1;
            ALARM_LED = 1; 
            BUZZER = 1;    
        } else {
            alarm_active = 0;
            ALARM_LED = 0;
            BUZZER = 0;
        }
    }

    // 2. EXTERNAL INTERRUPT (RB0/INT): Ultrasonic Echo
    if (INTE && INTF) {
        if (echo_state == 0) {
            // RISING EDGE: Sound wave just fired
            TMR1H = 0;
            TMR1L = 0;
            TMR1ON = 1;              // Start counting microseconds
            OPTION_REG &= ~(1 << 6); // Flip INTEDG bit to 0 (Look for Falling Edge next)
            echo_state = 1;
        } else {
            // FALLING EDGE: Sound wave returned
            TMR1ON = 0;              // Stop counting
            unsigned int time_taken = (TMR1H << 8) | TMR1L;
            dist_cm = time_taken / 58; // Calculate distance
            
            OPTION_REG |= (1 << 6);  // Flip INTEDG bit back to 1 (Look for Rising Edge)
            echo_state = 0;
        }
        INTF = 0; // Clear the interrupt flag
    }
    
    // 3. TIMER1 OVERFLOW: Sensor Timeout (In case the sound wave is lost forever)
    if (TMR1IE && TMR1IF) {
        TMR1ON = 0;
        echo_state = 0;
        OPTION_REG |= (1 << 6); // Reset to looking for Rising Edge
        TMR1IF = 0; // Clear flag
    }
}

// --- LCD HELPER FUNCTIONS ---
void lcd_enable_pulse(void) {
    E = 1;
    __delay_us(10);
    E = 0;
    __delay_us(100);
}

void lcd_send_nibble(unsigned char nibble) {
    D4 = (nibble >> 0) & 0x01;
    D5 = (nibble >> 1) & 0x01;
    D6 = (nibble >> 2) & 0x01;
    D7 = (nibble >> 3) & 0x01;
    lcd_enable_pulse();          
}

void lcd_command_4bit(unsigned char cmd) {
    RS = 0;                      
    lcd_send_nibble(cmd >> 4);  
    lcd_send_nibble(cmd & 0x0F);
    __delay_ms(2);              
}

void lcd_data_4bit(unsigned char data) {
    RS = 1;                      
    lcd_send_nibble(data >> 4);  
    lcd_send_nibble(data & 0x0F);
    __delay_ms(2);
}

void lcd_init_4bit(void) {
    TRISCbits.TRISC0 = 0;
    TRISCbits.TRISC1 = 0;
    TRISDbits.TRISD4 = 0;
    TRISDbits.TRISD5 = 0;
    TRISDbits.TRISD6 = 0;
    TRISDbits.TRISD7 = 0;
    __delay_ms(20);
    RS = 0;
    lcd_send_nibble(0x03);
    __delay_ms(5);
    lcd_send_nibble(0x03);
    __delay_us(150);
    lcd_send_nibble(0x03);
    __delay_us(150);
    lcd_send_nibble(0x02);
    __delay_us(150);
    lcd_command_4bit(0x28);
    lcd_command_4bit(0x0C);
    lcd_command_4bit(0x01);
    __delay_ms(2);
    lcd_command_4bit(0x06);
}

void lcd_set_cursor_4bit(unsigned char row, unsigned char col) {
    unsigned char address;
    if (row == 1) address = 0x80 + col;
    else address = 0xC0 + col;
    lcd_command_4bit(address);
}

void lcd_string_4bit(const char *str) {
    while (*str != '\0') {
        lcd_data_4bit(*str);    
        str++;
    }
}

// --- ADC FUNCTIONS ---
void adc_init(void) {
    ADCON0 = 0x41; 
    ADCON1 = 0x8E; 
}

unsigned int adc_read(void) {
    __delay_ms(2);        
    GO_nDONE = 1;         
    while(GO_nDONE);      
    return ((ADRESH << 8) + ADRESL); 
}

// --- MAIN PROGRAM ---
void main(void) {
    char display_buffer[16];
    unsigned int adc_val;
    float voltage;
    int temp_c; 
    
    // Respiratory tracking variables
    unsigned int breaths_in_window = 0;
    unsigned int resp_rate_bpm = 0;
    unsigned char inhaling = 0;
    
    // Initializations
    lcd_init_4bit();
    adc_init();
    
    // Pin Directions
    TRISAbits.TRISA0 = 1; 
    TRIG_DIR = 0;         
    ECHO_DIR = 1;         
    ALARM_LED_DIR = 0; 
    BUZZER_DIR = 0;    
    
    ALARM_LED = 0; 
    BUZZER = 0;
    
    // --- CONFIGURE INTERRUPTS ---
    // Timer0 (Apnea Watchdog)
    OPTION_REG &= 0xC0; 
    OPTION_REG |= 0x07; // 1:256 prescaler
    TMR0IE = 1; 
    
    // External Interrupt RB0 (Ultrasonic Echo)
    OPTION_REG |= (1 << 6); // INTEDG = 1 (Interrupt on Rising Edge to start)
    INTE = 1; 
    
    // Timer1 (Ultrasonic Counter)
    T1CON = 0x00; 
    TMR1IE = 1; // Enable overflow interrupt just in case
    
    // Enable Global and Peripheral Interrupts
    PEIE = 1;   
    GIE = 1;    
        
    while(1) {
        
        // 1. Send the Trigger Pulse (Non-blocking!)
        if (echo_state == 0) { // Only fire if we aren't currently waiting for an echo
            TRIG = 1;
            __delay_us(10);
            TRIG = 0;
        }
        
        // 2. Breath Detection Logic (Using 'dist_cm' which is magically updated by the ISR)
        if (dist_cm > 0 && dist_cm < 50 && inhaling == 0) {
            inhaling = 1;
            breaths_in_window++;
            
            GIE = 0; 
            apnea_timer = 0; 
            GIE = 1; 
            
        } else if (dist_cm >= 50 && inhaling == 1) {
            inhaling = 0;
            GIE = 0; 
            apnea_timer = 0; 
            GIE = 1; 
        }
        
        // 3. Calculate BPM every 15 Seconds
        if (seconds_passed >= 15) {
            resp_rate_bpm = breaths_in_window * 4; 
            
            GIE = 0;
            breaths_in_window = 0;
            seconds_passed = 0;
            GIE = 1;
        }
        
        // 4. Read Temperature
        adc_val = adc_read();
        voltage = (adc_val * 5.0) / 1023.0;
        temp_c = (int)(voltage * 100.0);
        
        // 5. Update LCD Screen
        if (alarm_active) {
            sprintf(display_buffer, "WARNING: APNEA! ");
        } else {
            sprintf(display_buffer, "Resp: %u BPM    ", resp_rate_bpm);
        }
        lcd_set_cursor_4bit(1, 0);
        lcd_string_4bit(display_buffer);
        
        sprintf(display_buffer, "Temp: %d C     ", temp_c);
        lcd_set_cursor_4bit(2, 0);
        lcd_string_4bit(display_buffer);
        
        // A short 100ms delay to prevent the screen from flickering and give the sensor time to rest
        __delay_ms(100); 
    }
}