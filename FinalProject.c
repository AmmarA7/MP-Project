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

// DEFINING PINS 
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

// GLOBAL VARIABLES INITIATION
volatile unsigned int apnea_timer = 0;   
volatile unsigned int ms_counter = 0;    
volatile unsigned int seconds_passed = 0;
volatile unsigned char alarm_active = 0; 
volatile unsigned int dist_cm = 0;
volatile unsigned char echo_state = 0; 

// - I2C DRIVER FUNCTIONS
void i2c_init(void){      // here we basically configure the I2C pins and set it to master mode with a frequency of 100kHz
    TRISCbits.TRISC3 = 1;  // inputs
    TRISCbits.TRISC4 = 1; //  inputs
    SSPCON = 0x28;  // Enabling SSP port, binary is --> 0b00101000
    SSPCON2 =0x00;
    SSPSTAT =0x00;
    SSPADD = (_XTAL_FREQ / (4 * 100000)) - 1; //Set the I2C clock frequency to 100kHz
}

void i2c_wait(void) {
    while ((SSPSTAT & 0x04) || (SSPCON2 & 0x1F)); // Wait until the I2C bus is idle; no transmission or or sequence active.
}

void i2c_start(void) {
    i2c_wait();
    SEN = 1; // initiate Start condition in a function
}

void i2c_stop(void) {
    i2c_wait();
    PEN = 1; // same for stop condition initiate it
}

void i2c_write(unsigned char data) {
    i2c_wait();
    SSPBUF = data; // load data into buffer to transmit if bus is idle or ready
}


unsigned char i2c_read(unsigned char ack) {
    unsigned char temp;
    i2c_wait();
    RCEN = 1; // receive mode enable is positive, enabled receive
    i2c_wait();
    temp = SSPBUF; // read data from buffer, again if the bus is ready or idle
    i2c_wait();
    ACKDT = (ack) ? 0 : 1; // acknowledge the bit (is it 0 = ACK, or 1 = NACK)
    ACKEN = 1; // send the acknowledge sequence to confirm the byte was received
    return temp;
}

          // up to here the I2C driver library is built, marking its fundamental building blocks to operate in the first place

// MAX30102 (pulse / heart beat sensor) PING TEST FUNCTION   
unsigned char ping_max30102(void){
    
    unsigned char part_id;
    
    i2c_start();
    i2c_write(0xAE); // MAX30102 I2C Write Address (0xAE) with 8 bits and the 8th is the write flag
    i2c_write(0xFF); // Point to Part ID Register (0xFF)
    
    i2c_start();     // Repeated start for reading
    i2c_write(0xAF); // MAX30102 I2C Read Address (0xAF)
    part_id = i2c_read(0); // Read 1 byte, send NACK to finish
    i2c_stop();
    
    return part_id;
} // this function is merely written to ensure the 
//   communication between the pulse sensor and the PIC microcontroller
 //  are communicating properly over the I2C bus.


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
// this above was the interrupt service routine which basically tracks time for the breathing activity using the ultrasonic
// sensor distance measurement. it triggers a buzzer and an led to notify the nurses whenever the patient stops breathing, which is done by 
// constantly measuring the distance that changes with respect to time, if the distance is constant, the interrupt flag is immediately raised.

// LCD functions, in 4-bit mode so nibbles are used
void lcd_enable_pulse(void) { 
    
    E = 1;                 // enable function is the enter key for the LCD, and it ignores the data until it reads the 1 pulse sent and then waits 10us
    __delay_us(10);
    E = 0;
    __delay_us(100);
}
void lcd_send_nibble(unsigned char nibble) {  // here this function takes a nibble (4 bits) and divides its bits through the 4 pins and calls the enable pulse function
    D4 = (nibble >> 0) & 0x01;               //  to force the LCD to read or in other words catch the high or low bit on each bit.
    D5 = (nibble >> 1) & 0x01;
    D6 = (nibble >> 2) & 0x01;
    D7 = (nibble >> 3) & 0x01;
    lcd_enable_pulse();          
}
void lcd_command_4bit(unsigned char cmd) {
    RS = 0;                      // this function tells the LCD that a command is incoming not a display text, so it sets RS to low to let it know when called.
    
    lcd_send_nibble(cmd >> 4);  // <-- send first 4 bits
    lcd_send_nibble(cmd & 0x0F); // <-- send second 4 bits
    __delay_ms(2);              
}
void lcd_data_4bit(unsigned char data) { // this one does the reverse of the function above it 
    RS = 1;                             //  and tells the LCD that a display data is incoming not a command, by setting RS to 1.
    lcd_send_nibble(data >> 4);  
    lcd_send_nibble(data & 0x0F);
    __delay_ms(2);
}
void lcd_init_4bit(void) {
    TRISCbits.TRISC0 = 0;
    TRISCbits.TRISC1 = 0;
    TRISDbits.TRISD4 = 0;
    TRISDbits.TRISD5 = 0;
    TRISDbits.TRISD6 = 0;     // data is sent through pins D4-D7
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
    lcd_command_4bit(0x28); // here the LCD is set to 2 line mode
    lcd_command_4bit(0x0C); // this turns the display ON, cursor OFF, and cursor blink OFF.
    lcd_command_4bit(0x01); // command to clear entire screen of the LCD.
    __delay_ms(2);
    lcd_command_4bit(0x06); // this basically sets the entry mode to auto increment the cursor location to the right after every character that is printed.
}
void lcd_set_cursor_4bit(unsigned char row, unsigned char col) {
    unsigned char address;
    if (row == 1) address = 0x80 + col;   //the internal DDRAM memory of a the LCD maps Row 1 to starting memory address 0x00 and row 2 to the address 0x40.
                                         // to tell the LCD controller that we want to alter the cursor address, the highest bit must be high, adding a base offset of 0x80.
    else address = 0xC0 + col;
    lcd_command_4bit(address);
}
void lcd_string_4bit(const char *str) {
    while (*str != '\0') {     // while string did not end; end of array, print the next character and got eh one after it.
        lcd_data_4bit(*str);    
        str++;
    }
}

// ADC FUNCTIONS
void adc_init(void) {
    ADCON0 = 0x41;  // configuring the 2 ADCON registers, 0b01000001 in binary, and each register bit configures something different.
    ADCON1 = 0x8E; 
}
unsigned int adc_read(void) {   
    __delay_ms(2);        
    GO_nDONE = 1;         
    while(GO_nDONE);      
    return ((ADRESH << 8) + ADRESL); // after we shift ADRESH 8 positions to the left,
                                    //  and after adding adding ADRESL we end up with a single integer between 0 and 1023.
}

// MAIN PROGRAM
void main(void) {
    char display_buffer[16];
    
    // Initializations
    lcd_init_4bit();  // initializing LCD in 4-bit mode
    adc_init();   // initializing the ADC
    i2c_init(); // initializing the I2C bus
    
    // pin directions, 1 for input and 0 for output.
    TRISAbits.TRISA0 = 1; 
    TRIG_DIR = 0;         
    ECHO_DIR = 1;         
    ALARM_LED_DIR = 0; 
    BUZZER_DIR = 0;    
    ALARM_LED = 0; 
    BUZZER = 0;
    
    // Check MAX30102 Connection Before Starting
    unsigned char max_id = ping_max30102() ;  
    sprintf(display_buffer, "MAX ID: 0x%02X", max_id);
    lcd_set_cursor_4bit(1, 0);
    lcd_string_4bit(display_buffer);
    
    if (max_id == 0x15) {
        lcd_set_cursor_4bit(2, 0);
        lcd_string_4bit("I2C SUCCESS!    ") ;
    } else {
        lcd_set_cursor_4bit(2, 0);
        lcd_string_4bit("I2C FAILED!     ");
    }   // this above just checks if the pulse sensor is working by sending and receiving an address id and checking if it matches.
    
    // pause for 3 seconds so you can read the result before the Apnea code takes over, which either says the I@C is working fine or otherwise.
    __delay_ms(1000); // 1 second delay
    __delay_ms(1000);
    __delay_ms(1000);
    lcd_command_4bit(0x01); // call the clear screen command.
    
    // CONFIGURING INTERRUPTS
    OPTION_REG &= 0xC0; // interrupt OPTION register is configured
    OPTION_REG |= 0x07; 
    TMR0IE = 1; // enabled timer 0 interrupts.
    OPTION_REG |= (1 << 6); 
    INTE = 1; 
    T1CON = 0x00; // reset register settings for timer one.
    TMR1IE = 1;  // enabled timer 1 interrupts.
    PEIE = 1;   // peripheral interrupts enabled.
    GIE = 1;   // global interrupts enabled.
    
    unsigned int breaths_in_window = 0;
    unsigned int resp_rate_bpm = 0;      // variables for breath counting and temperature measuring.
    unsigned char inhaling = 0;
    unsigned int adc_val;
    float voltage;
    int temp_c; 
        
    while(1) {
        if (echo_state ==  0) { // when the echo state is 0 or when it finishes a measurement, the ultrasonic sensor will fire a 10us long high wave triggered by the TRIG pin.
            TRIG = 1;          // then the ISR will handle the distance measured.
            __delay_us(10);
            TRIG = 0;
        }
        
        if (dist_cm > 0 && dist_cm < 50 && inhaling == 0) {  // if the measured distance drops below 50cm (this is the distance that the ultrasonic sensor should be
                                                            // set from the patient's chest); (meaning the patient's chest expanded
            inhaling = 1;                                  // closer to the sensor) and the system wasn't already tracking an existing inhalation
            breaths_in_window++;                          // (inhaling == 0), it registers a new breath cycle.)
            GIE = 0; 
            apnea_timer = 0; 
            GIE = 1; 
        } else if (dist_cm >= 50 && inhaling == 1) { // if they exhale the breath cycle goes back to zero.
            inhaling = 0;
            GIE = 0; 
            apnea_timer = 0; 
            GIE = 1; 
        }
        
        if (seconds_passed >= 15) {
            resp_rate_bpm = breaths_in_window * 4;  // simple calculation to get BPM by multiplying the breaths in 15 secs by 4.
            GIE = 0;
            breaths_in_window = 0;
            seconds_passed = 0;
            GIE = 1;
        }
        
        adc_val = adc_read();
        voltage = (adc_val * 5.0) / 1023.0;  // simple temperature calculation in Celsius.
        temp_c = (int)(voltage * 100.0);
        
        if (alarm_active) {
            sprintf(display_buffer, "WARNING: APNEA! ");     // this here evaluates the global safety flag alarm_active (which is forced high by the background 
        } else {                                            // ISR if the apnea_timer ever climbs past 90 ticks without a breath resetting it). 
            sprintf(display_buffer, "Resp: %u BPM    ", resp_rate_bpm);
            
           // If a medical emergency is active, Row 1 flashes a major warning message. If breathing is fine, 
          //  it displays the steady respiration rate alongside the calculated temperature on Row 2 before pausing for 100ms to repeat the loop smoothly.
        }
        lcd_set_cursor_4bit(1, 0);
        lcd_string_4bit(display_buffer); 
        
        sprintf(display_buffer, "Temp: %d C     ", temp_c);
        lcd_set_cursor_4bit(2, 0);
        lcd_string_4bit(display_buffer);
        
        __delay_ms(100); 
    }
}