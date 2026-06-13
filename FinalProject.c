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
#define RS RC1  // LCD RS
#define E RC0   // LCD Enable
#define D4 RD4
#define D5 RD5
#define D6 RD6
#define D7 RD7

// HC-SR04 Pins
#define TRIG RB0
#define ECHO RB1
#define TRIG_DIR TRISBbits.TRISB0
#define ECHO_DIR TRISBbits.TRISB1

// Alarm Pins (NEW)
#define ALARM_LED RD0
#define BUZZER RD1
#define ALARM_LED_DIR TRISDbits.TRISD0
#define BUZZER_DIR TRISDbits.TRISD1

// --- GLOBAL VARIABLES FOR ISR ---
volatile unsigned int apnea_timer = 0;   // Tracks time since last breath
volatile unsigned int ms_counter = 0;    // Helps track exact seconds
volatile unsigned int seconds_passed = 0;// Tracks our 15-second measurement window
volatile unsigned char alarm_active = 0; 

// --- INTERRUPT SERVICE ROUTINE (ISR) ---
void __interrupt() ISR(void) {
    // Check if Timer0 caused the interrupt
    if (TMR0IE && TMR0IF) {
        TMR0IF = 0; // Clear the interrupt flag
        
        // With 4MHz clock and 1:256 prescaler, Timer0 overflows every 65.5ms
        ms_counter++;
        apnea_timer++;
        
        // Count full seconds (roughly 15 ticks of 65.5ms = 1 second)
        if (ms_counter >= 15) {
            ms_counter = 0;
            seconds_passed++;
        }

        // ALARM LOGIC: If no breath detected for ~6 seconds (90 ticks)
        if (apnea_timer > 90) {
            alarm_active = 1;
            ALARM_LED = 1; // Turn ON warning light
            BUZZER = 1;    // Turn ON buzzer
        } else {
            alarm_active = 0;
            ALARM_LED = 0;
            BUZZER = 0;
        }
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

// --- ULTRASONIC SENSOR FUNCTION ---
unsigned int get_distance(void) {
    unsigned int time_taken, distance;
    
    TRIG = 1;
    __delay_us(10);
    TRIG = 0;
    
    TMR1H = 0;
    TMR1L = 0;
    TMR1ON = 1; 
    
    while(ECHO == 0) {
        if(TMR1H > 10) { 
            TMR1ON = 0;
            return 0;    
        }
    }
    
    TMR1ON = 0;
    TMR1H = 0;
    TMR1L = 0;
    TMR1ON = 1;
    
    while(ECHO == 1) {
        if(TMR1H > 100) break;
    }
    TMR1ON = 0;
    
    time_taken = (TMR1H << 8) | TMR1L;
    distance = time_taken / 58; 
    
    return distance;
}

// --- MAIN PROGRAM ---
void main(void) {
    char display_buffer[16];
    unsigned int dist_cm;
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
    ALARM_LED_DIR = 0; // Output
    BUZZER_DIR = 0;    // Output
    
    ALARM_LED = 0; // Start with alarms OFF
    BUZZER = 0;
    
    // Configure Timer0 for the ISR Watchdog
    OPTION_REG &= 0xC0; // Assign prescaler to Timer0
    OPTION_REG |= 0x07; // 1:256 prescaler
    TMR0 = 0;
    TMR0IE = 1; // Enable Timer0 interrupt
    PEIE = 1;   // Enable peripheral interrupts
    GIE = 1;    // Enable global interrupts
    
    T1CON = 0x00; // Timer1 for Ultrasonic
        
    while(1) {
        // 1. Read Distance (Chest Position)
        dist_cm = get_distance();
        
        // 2. Breath Detection Logic
        // Assuming normal distance is 50cm. Drops to <48cm when chest expands (inhale)
        if (dist_cm > 0 && dist_cm < 48 && inhaling == 0) {
            inhaling = 1;
            breaths_in_window++;
            
            // RESET THE ALARM TIMER! We detected a breath.
            GIE = 0; // Briefly disable interrupts to avoid corruption
            apnea_timer = 0; 
            GIE = 1; 
            
        } else if (dist_cm >= 49 && inhaling == 1) {
            // Chest returned to resting position (exhale)
            inhaling = 0;
        }
        
        // 3. Calculate BPM every 15 Seconds
        if (seconds_passed >= 15) {
            resp_rate_bpm = breaths_in_window * 4; // Multiply by 4 for 1-minute rate
            
            // Reset for the next 15 second window
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
        
        // Read fast (10 times a second) to catch chest movement
        __delay_ms(100); 
    }
}