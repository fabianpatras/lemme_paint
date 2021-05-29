// include TFT and SPI libraries
#include <TFT.h>  
#include <SPI.h>

/* ~~~~~~~~~~~~~~~~~~~~~~~~~~~ DEFINES ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */
// sensor pins

// sensor 0
#define ECHO0 2
#define TRIG0 3

// sensor 1
#define ECHO1 5
#define TRIG1 4

// sensor 2
#define ECHO2 6
#define TRIG2 7

// sensor 3
#define ECHO3 A1
#define TRIG3 A0

// sensor 4
#define ECHO4 A2
#define TRIG4 A3

// pin definition for Arduino UNO
#define cs   10
#define dc   9
#define rst  8

// numbers - constants
#define NO_OF_SENSORS 5
#define NO_OF_MEASUREMENTS 5
#define NO_OF_HIST_MEASUREMENTS 5

// max distance = 40 cm?? = 160 * 1/4 cm
#define INFINITY 160

// some magic defines for median of 5 elements
#define swap(a,b) (a) ^= (b); (b) ^= (a); (a) ^= (b);
#define sort(a,b) if((a)>(b)){ swap((a),(b)); }

#define MIN_2(a, b) (a) < (b) ? (a) : (b)

#define MIN_3(a, b, c) (a) < (b) ? \
                        ((a) < (c) ? (a) : (c)) : \
                        ((b) < (c) ? (b) : (c))


// kalman parameters
#define INIT_COV_EST 10.0

/* ~~~~~~~~~~~~~~~~~~~~~~~~~~~ VARIABLES ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

// echo pins
int echo_pins[] = {ECHO0, ECHO1, ECHO2, ECHO3, ECHO4};

// trig pins
int trig_pins[] = {TRIG0, TRIG1, TRIG2, TRIG3, TRIG4};

int raw_data[NO_OF_SENSORS];
int measurements[NO_OF_SENSORS][NO_OF_MEASUREMENTS];
int results[NO_OF_SENSORS];
int last_results[NO_OF_SENSORS];

int history[NO_OF_SENSORS][NO_OF_HIST_MEASUREMENTS] = {INFINITY};

int history_idx = 0;

int screen_width;
int screen_height;

int crt_x;
int crt_y;

int last_x;
int last_y;

int debug = 0;

// Kalman Filtering stuff
const double R = 40.0;
const double H = 1.00;
// initial covariance estimate
double Q[NO_OF_SENSORS];
// initial error estimate
double P[NO_OF_SENSORS];
// initial readings estimate
double U_hat[NO_OF_SENSORS];
// initial kalman gain
double K[NO_OF_SENSORS];

double Q_2[2];
double P_2[2];
double U_hat_2[2];
double K_2[2];

// create an instance of the library
TFT TFTscreen = TFT(cs, dc, rst);


/* ~~~~~~~~~~~~~~~~~~~~~~~~~~~ SET UP FUNCTIONS ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

void init_sensors_pins()
{
        for (int i = 0; i < NO_OF_SENSORS; i++) {
                pinMode(echo_pins[i], INPUT);
                pinMode(trig_pins[i], OUTPUT);
        }

}

void init_kalman_params()
{
        for (int i = 0; i < NO_OF_SENSORS; i++) {
                Q[i] = INIT_COV_EST;
                P[i] = 0.0;
                U_hat[i] = INFINITY;
                K[i] = 0.0;
        }
        for (int i = 0; i < 2; i++) {
                Q_2[i] = INIT_COV_EST;
                P_2[i] = 0.0;
                U_hat_2[i] = INFINITY;
                K_2[i] = 0.0;
        }
}


/* ~~~~~~~~~~~~~~~~~~~~~~~~~~~ MEASUREMENT FUCNTIONS ~~~~~~~~~~~~~~~~~~~~~~~~ */

void clear_measurements()
{
        for (int i = 0; i < NO_OF_SENSORS; i++) {
                for (int j = 0; j < NO_OF_MEASUREMENTS; j++) {
                        measurements[i][j] = INFINITY;
                }
                results[i] = INFINITY;
                last_results[i] = INFINITY;
        }
}

void median(int arr[])
{
        sort(arr[0], arr[1]);
        sort(arr[1], arr[4]);  
        sort(arr[0], arr[2]);
        sort(arr[1], arr[2]);
        sort(arr[0], arr[3]);  
        sort(arr[2], arr[3]);
        sort(arr[1], arr[4]);
        sort(arr[1], arr[2]);
}

void transfers_results_to_old()
{
        for (int i = 0; i < NO_OF_SENSORS; i++) {
                last_results[i] = results[i];
        }
}

// sensor number i is between 0 and NO_OF_SENSORS
unsigned int sensor_distance(unsigned int sensor_number)
{
        debug++;
        unsigned int trig = trig_pins[sensor_number];
        unsigned int echo = echo_pins[sensor_number];
        
        unsigned int distance = 0;

        digitalWrite(trig, LOW);
        delayMicroseconds(2);
        digitalWrite(trig, HIGH);
        delayMicroseconds(10);
        digitalWrite(trig, LOW);
        
        unsigned long duration = pulseIn(echo, HIGH);
        // TODO: fine tune me to get distance in pixels, not in quarters
        // of centimeters
        distance = 2 * duration * 0.034; // instead of cm will be 1/4 of cm
        return distance;
}

void sanitize_readings()
{
        // reoreder measurements
        for (int i = 0; i < NO_OF_SENSORS; i++) {
                median(measurements[i]);
                // store results
                results[i] = measurements[i][2];
                history[i][history_idx % NO_OF_HIST_MEASUREMENTS] = results[i];
        }
        history_idx++;
}

// reads all the sensors five times and gets the median for each sensors
// output is stored in `int results[]`
void read_sensors()
{
        // for each of (5 times) cycle through each sensor
        for (int i = 0; i < NO_OF_MEASUREMENTS; i++) {
        
                // read each sensor back-to-back
                for (int j = 0; j < NO_OF_SENSORS; j++) {
                        // sensors are indexed from 0 to NO_OF_SENSORS - 1
                        measurements[j][i] = sensor_distance(j);
                }
        }
        sanitize_readings();

        crt_x = MIN_2(results[0], results[1]);
        crt_y = MIN_3(results[2], results[3], results[4]);
}

// reads all the sensors and stores the results into `int raw_data[]`
void read_sensors_raw()
{
        // read each sensor back-to-back
        for (int j = 0; j < NO_OF_SENSORS; j++) {
                // sensors are indexed from 0 to NO_OF_SENSORS - 1
                raw_data[j] = sensor_distance(j);
        }
}

void read_wrapper()
{
        int acc;
        // populate history array with sensor readings
        for (int i = 0; i < NO_OF_HIST_MEASUREMENTS; i++) {
                read_sensors();
        }

        // get the avg
        for (int i = 0; i < NO_OF_SENSORS; i++) {
                acc = 0;
                for (int j = 0; j < NO_OF_HIST_MEASUREMENTS; j++) {
                        acc += history[i][j];
                }
                results[i] = acc / NO_OF_HIST_MEASUREMENTS;
        }

        crt_x = MIN_2(results[0], results[1]);
        crt_y = MIN_3(results[2], results[3], results[4]);
}


// https://github.com/rizkymille/ultrasonic-hc-sr04-kalman-filter/blob/master/hc-sr04_kalman_filter/hc-sr04_kalman_filter.ino

double kalman(double U, int s){
        // TODO: fine-tune H = 1.00, R = 40 atm
        K[s] = P[s] * H/(H * P[s] * H + R);
        U_hat[s] += + K[s] * (U - H * U_hat[s]);
        P[s] = (1 - K[s] * H) * P[s] + Q[s];
        return U_hat[s];
}

double kalman_2(double U, int s){
        // TODO: fine-tune H = 1.00, R = 40 atm
        K_2[s] = P_2[s] * H/(H * P_2[s] * H + R);
        U_hat_2[s] += + K_2[s] * (U - H * U_hat_2[s]);
        P_2[s] = (1 - K_2[s] * H) * P_2[s] + Q_2[s];
        return U_hat_2[s];
}

void kalman_read()
{
        double clear_reading = 0.0;
        double kalman_reading = 0.0;
        for (int i = 0; i < NO_OF_SENSORS; i++) {
                clear_reading = sensor_distance(i);
                kalman_reading = kalman(clear_reading, i);
                Serial.print(i);
                Serial.print("    ");
                Serial.print(clear_reading);
                Serial.print("    ");
                Serial.println(kalman_reading);
        }
}

// results are stored in U_hat
void kalman_raw()
{
        read_sensors_raw();
        
        double clear_reading = 0.0;
        double kalman_reading = 0.0;
        for (int i = 0; i < NO_OF_SENSORS; i++) {
                kalman(raw_data[i], i);
        }
}

void kalman_wrapper()
{
        // kalman filter fore ach sensor individually
        kalman_read();

        // calculate position
        crt_x = MIN_2(U_hat[0], U_hat[1]);
        crt_y = MIN_3(U_hat[2], U_hat[3], U_hat[4]);

        // kalman on position
        crt_x = kalman_2(crt_x, 0);
        crt_y = kalman_2(crt_y, 1);

        Serial.print(crt_x);
        Serial.print("    ");
        Serial.println(crt_y);
}


void kalman_raw_wrapper()
{
        kalman_raw();
        // calculate position
        crt_x = MIN_2(U_hat[0], U_hat[1]);
        crt_y = MIN_3(U_hat[2], U_hat[3], U_hat[4]);

        // kalman on position
        crt_x = kalman_2(crt_x, 0);
        crt_y = kalman_2(crt_y, 1);
}

/* ~~~~~~~~~~~~~~~~~~~~~~~~~~~ DARWING FUNCTIONS ~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

void random_collor()
{
        int r = random() % 256;
        int g = random() % 256;
        int b = random() % 256;

        TFTscreen.stroke(b, g, r);
}

/*
void draw_test_lines(unsigned int distance1, unsigned int distance2)
{
        if (last_distance1 == distance1) {
//               return;
        } else {
                TFTscreen.stroke(0, 0, 0);
                TFTscreen.line(last_distance1, 0, last_distance1, screen_height);
                last_distance1 = distance1;
                
                TFTscreen.stroke(0, 0, 255);
                TFTscreen.line(distance1, 0, distance1, screen_height);
        }
        if (last_distance2 == distance2) {
        //       return;
        } else {
                TFTscreen.stroke(0, 0, 0);
                TFTscreen.line(last_distance2, 0, last_distance2, screen_height);
                last_distance2 = distance2;
                
                TFTscreen.stroke(0, 255, 0);
                TFTscreen.line(distance2, 0, distance2, screen_height);
        }
}

*/

/* ~~~~~~~~~~~~~~~~~~~~~~~~~~~ DEBUG FUNCTIONS ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

void print_measurements()
{

        char buff[100];
        sprintf(buff, "s[%d] = [%d] \t"
                "s[%d] = [%d] \t"
                "s[%d] = [%d] \t"
                "s[%d] = [%d] \t"
                "s[%d] = [%d] \t",
                0, results[0],
                1, results[1],
                2, results[2],
                3, results[3],
                4, results[4]);
        Serial.println(buff);
}

/* ~~~~~~~~~~~~~~~~~~~~~~~~~~~ SETUP ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

void setup() {

        // pin mode and other stuff
        init_sensors_pins();
        
        // measurements
        clear_measurements();

        // kalman stuff
        init_kalman_params();
        
        //initialize the library
        TFTscreen.begin();
        
        // clear the screen with a black background
        TFTscreen.background(0, 0, 0);
        //set the text size
        TFTscreen.setTextSize(2);
        
        screen_width = TFTscreen.width();
        screen_height = TFTscreen.height();
        
        
        // set a random font color
        TFTscreen.stroke(0, 0, 255);
        
        Serial.begin(9600);
}

/* ~~~~~~~~~~~~~~~~~~~~~~~~~~~ LOOP ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

void loop() {
        long t1 = micros();
        random_collor();

        kalman_wrapper();

//        kalman_read();
//        crt_x = MIN_2(U_hat[0], U_hat[1]);
//        crt_y = MIN_3(U_hat[2], U_hat[3], U_hat[4]);
        
//        read_wrapper();
//        crt_x = MIN_3(results[2], results[3], results[4]);
//        crt_y = MIN_2(results[0], results[1]);
        TFTscreen.circle(screen_width - crt_x, crt_y, 3);
//        print_measurements();
        long t2 = micros();
        
        Serial.println((t2 - t1) / 1000);
        Serial.println(debug);
        debug = 0;
//        Serial.println((t2 - t1) / 1000);
//        Serial.println(MIN_3(1,2,3));
//        Serial.println(MIN_3(2,3,1));
//        Serial.println(MIN_3(3,1,2));
}

/* EOF */
