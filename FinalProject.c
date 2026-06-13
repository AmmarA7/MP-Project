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

#define TRIG RB1 
#define ECHO RB0 
#define TRIG_DIR TRISBbits.TRISB1
#define ECHO_DIR TRISBbits.TRISB0

#define ALARM_LED RD0
#define BUZZER RD1
#define ALARM_LED_DIR TRISDbits.TRISD0
#define BUZZER_DIR TRISDbits.TRISD1

// --- GLOBAL VARIABLES ---
volatile unsigned int apnea_timer = 0;   
volatile unsigned int ms_counter = 0;    
volatile unsigned int seconds_passed = 0;
volatile unsigned char alarm_active = 0; 
volatile unsigned int dist_cm = 0;
volatile unsigned char echo_state = 0; 

// --- I2C DRIVER FUNCTIONS (NEW) ---
void i2c_init(void) {
    TRISCbits.TRISC3 = 1; // SCL must be input
    TRISCbits.TRISC4 = 1; // SDA must be input
    SSPCON = 0x28;        // Enable SSP port, I2C Master mode
    SSPCON2 = 0x00;
    SSPSTAT = 0x00;
    SSPADD = (_XTAL_FREQ / (4 * 100000)) - 1; // Set I2C clock to 100kHz
}

void i2c_wait(void) {
    while ((SSPSTAT & 0x04) || (SSPCON2 & 0x1F)); // Wait until I2C bus is idle
}

void i2c_start(void) {
    i2c_wait();
    SEN = 1; // Initiate Start condition
}

void i2c_stop(void) {
    i2c_wait();
    PEN = 1; // Initiate Stop condition
}

void i2c_write(unsigned char data) {
    i2c_wait();
    SSPBUF = data; // Load data into buffer to transmit
}

unsigned char i2c_read(unsigned char ack) {
    unsigned char temp;
    i2c_wait();
    RCEN = 1; // Enable receive mode
    i2c_wait();
    temp = SSPBUF; // Read data from buffer
    i2c_wait();
    ACKDT = (ack) ? 0 : 1; // Acknowledge bit (0 = ACK, 1 = NACK)
    ACKEN = 1; // Send Acknowledge sequence
    return temp;
}

// --- MAX30102 PING TEST FUNCTION (NEW) ---
unsigned char ping_max30102(void) {
    unsigned char part_id;
    
    i2c_start();
    i2c_write(0xAE); // MAX30102 I2C Write Address (0xAE)
    i2c_write(0xFF); // Point to Part ID Register (0xFF)
    
    i2c_start();     // Repeated start for reading
    i2c_write(0xAF); // MAX30102 I2C Read Address (0xAF)
    part_id = i2c_read(0); // Read 1 byte, send NACK to finish
    i2c_stop();
    
    return part_id;
}


// --- INTERRUPT SERVICE ROUTINE ---
void __interrupt() ISR(void) {
    if (TMR0IE && TMR0IF) {
        TMR0IF = 0; 
        ms_counter++;
        apnea_timer++;
        
        if (ms_counter >= 15) {
            ms_counter = 0;
            seconds_passed++;
        }
        if (apnea_timer > 90) { 
            alarm_active = 1;
            ALARM_LED = 1; 
            BUZZER = 1;    
        } else {
            alarm_active = 0;
            ALARM_LED = 0;
            BUZZER = 0;
        }
    }

    if (INTE && INTF) {
        if (echo_state == 0) {
            TMR1H = 0;
            TMR1L = 0;
            TMR1ON = 1;              
            OPTION_REG &= ~(1 << 6); 
            echo_state = 1;
        } else {
            TMR1ON = 0;              
            unsigned int time_taken = (TMR1H << 8) | TMR1L;
            dist_cm = time_taken / 58; 
            OPTION_REG |= (1 << 6);  
            echo_state = 0;
        }
        INTF = 0; 
    }
    
    if (TMR1IE && TMR1IF) {
        TMR1ON = 0;
        echo_state = 0;
        OPTION_REG |= (1 << 6); 
        TMR1IF = 0; 
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
    
    // Initializations
    lcd_init_4bit();
    adc_init();
    i2c_init(); // Initialize the I2C bus!
    
    // Pin Directions
    TRISAbits.TRISA0 = 1; 
    TRIG_DIR = 0;         
    ECHO_DIR = 1;         
    ALARM_LED_DIR = 0; 
    BUZZER_DIR = 0;    
    ALARM_LED = 0; 
    BUZZER = 0;
    
    // Check MAX30102 Connection Before Starting
    unsigned char max_id = ping_max30102();
    sprintf(display_buffer, "MAX ID: 0x%02X", max_id);
    lcd_set_cursor_4bit(1, 0);
    lcd_string_4bit(display_buffer);
    
    if (max_id == 0x15) {
        lcd_set_cursor_4bit(2, 0);
        lcd_string_4bit("I2C SUCCESS!    ");
    } else {
        lcd_set_cursor_4bit(2, 0);
        lcd_string_4bit("I2C FAILED!     ");
    }
    
    // Pause for 3 seconds so you can read the result before the Apnea code takes over
    __delay_ms(1000);
    __delay_ms(1000);
    __delay_ms(1000);
    lcd_command_4bit(0x01); // Clear screen
    
    // --- CONFIGURE INTERRUPTS ---
    OPTION_REG &= 0xC0; 
    OPTION_REG |= 0x07; 
    TMR0IE = 1; 
    OPTION_REG |= (1 << 6); 
    INTE = 1; 
    T1CON = 0x00; 
    TMR1IE = 1; 
    PEIE = 1;   
    GIE = 1;    
    
    unsigned int breaths_in_window = 0;
    unsigned int resp_rate_bpm = 0;
    unsigned char inhaling = 0;
    unsigned int adc_val;
    float voltage;
    int temp_c; 
        
    while(1) {
        if (echo_state == 0) { 
            TRIG = 1;
            __delay_us(10);
            TRIG = 0;
        }
        
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
        
        if (seconds_passed >= 15) {
            resp_rate_bpm = breaths_in_window * 4; 
            GIE = 0;
            breaths_in_window = 0;
            seconds_passed = 0;
            GIE = 1;
        }
        
        adc_val = adc_read();
        voltage = (adc_val * 5.0) / 1023.0;
        temp_c = (int)(voltage * 100.0);
        
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
        
        __delay_ms(100); 
    }
}