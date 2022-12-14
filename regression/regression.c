#include "contiki.h"

#include <stdio.h>
#include <stdlib.h>
#include <random.h>

#include "dev/light-sensor.h"
#include "dev/sht11-sensor.h"

/*
 * String formatting helper functions
 * Extracting Integer and Flat parts
 */
unsigned int MEASUREMENTS_PER_SECOND = 2;
float LOW_ACTIVITY_THRESHOLD = 1000.00;
float HIGH_ACTIVITY_THRESHOLD = 3000.00;

// Used for extracting integer part of the float
long extractInteger(float f) {
    return ((long) f);
}

// Used to extract the fraction part of the float
unsigned int extractFraction(float f) {
    int fractionPart = (int) 1000 * (f - extractInteger(f));
    return (abs(fractionPart));
}

// Calculates square root
float sRoot(float number) {
    float difference = 0.0;
    float error = 0.001;  // error tolerance
    float x = 10.0;       // initial guess
    int   i;
    for (i=0; i<50; i++) {
        x = 0.5 * (x + number/x);
        difference = x*x - number;
        if (difference<0) difference = -difference;
        if (difference<error) break; // the difference is deemed small enough
    }
    return x;
}

/*
 * FIFO Queue Implementation
 * All required helper methods included
 */

// FIFO queue structure definition
struct FIFOQueue {
    unsigned int capacity;
    int size;
    int last;
    float el[12];
};

// Light data access object definition
struct FIFOQueue lightDao = {
        12,
        0,
        11,
        { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }
};

// Temp data access object definition
struct FIFOQueue tempDao = {
        12,
        0,
        11,
        { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }
};

// Enqueues light measurements and automatically dequeues the first reading
void queueLightMeasurement(float item) {
    int x;
    for (x=(lightDao.last - 1); x >= 0; x--) {
        lightDao.el[x + 1] = lightDao.el[x];
    }
    lightDao.el[0] = item;
    if (lightDao.size < (lightDao.capacity - 1))
        lightDao.size = lightDao.size + 1;
}

// Enqueues temp measurements and automatically dequeues the first reading
void queueTempMeasurement(float item) {
    int x;
    for (x=(tempDao.last - 1); x >= 0; x--) {
        tempDao.el[x + 1] = tempDao.el[x];
    }
    tempDao.el[0] = item;
    if (tempDao.size < (tempDao.capacity - 1))
        tempDao.size = tempDao.size + 1;
}

// Prints elements in the FIFO buffer
void printElements(struct FIFOQueue dao, char dataType) {
    int i;
    printf("%c = [", dataType);
    for (i=0; i <= dao.last; i++){
        printf("%ld.%03u", extractInteger(dao.el[i]), extractFraction(dao.el[i]));
        if (i != dao.last) {
            printf(", ");
        }
    }
    printf("]\n");
}

// Prints log on high-activity level
void printHighActivityResults(struct FIFOQueue dao) {
    int i;
    printf("Light Readings Aggregation = None [ High Activity ]\n");
    printf("X = [");
    for (i=0; i <= dao.last; i++){
        printf("%ld.%03u", extractInteger(dao.el[i]), extractFraction(dao.el[i]));
        if (i != dao.last) {
            printf(", ");
        }
    }
    printf("]\n");
}

// Prints log on medium-activity level
void printMediumActivityResults(struct FIFOQueue dao) {
    float results[3];
    results[0] = (dao.el[0] + dao.el[1] + dao.el[2] + dao.el[3]) / 4.0;
    results[1] = (dao.el[4] + dao.el[5] + dao.el[6] + dao.el[7]) / 4.0;
    results[2] = (dao.el[8] + dao.el[9] + dao.el[10] + dao.el[11]) / 4.0;

    int i;
    printf("Light Readings Aggregation = 4-into-1 [ Medium Activity ]\n");
    printf("X = [");
    for (i=0; i < 3; i++){
        printf("%ld.%03u", extractInteger(results[i]), extractFraction(results[i]));
        if (i != 2) {
            printf(", ");
        }
    }
    printf("]\n");
}

// Prints log on low-activity level
void printLowActivityResults(struct FIFOQueue dao) {
    int i;
    float sum = 0.0;
    for (i = 0; i < dao.capacity; ++i) {
        sum += dao.el[i];
    }
    float result = sum / (float)dao.capacity;
    printf("Light Readings Aggregation = 12-into-1 [ Low Activity ]\n");
    printf("X = [ %ld.%03u ]\n", extractInteger(result), extractFraction(result));
}

// Calculates standard deviation of the population
float calculateStandardDeviation(struct FIFOQueue dao) {
    float sum = 0.0, mean, squareRootableValue, sumOfSquares = 0.0;
    int i;
    for (i = 0; i < dao.capacity; ++i) {
        sum += dao.el[i];
    }
    mean = sum / (float)dao.capacity;
    for (i = 0; i < dao.capacity; ++i) {
        sumOfSquares += (dao.el[i] - mean) * (dao.el[i] - mean);
    }
    squareRootableValue = sumOfSquares / dao.capacity;
    return sRoot(squareRootableValue);
}

// Computes regression equation and Mean Squared Error. Log results to the serial port
void calculateRegressionBetweenLightAndTemperature(struct FIFOQueue lightDao, struct FIFOQueue tempDao) {
    int i;
    unsigned int capacity = lightDao.capacity;
    float x, y, xy, xx, yy, slope, y_intercept, sumofSquaredError, mse;
    for(i=0; i < capacity; i++) {
        x += lightDao.el[i];
        y += tempDao.el[i];
        xy += (lightDao.el[i] * tempDao.el[i]);
        xx += (lightDao.el[i] * lightDao.el[i]);
        yy += (tempDao.el[i] * tempDao.el[i]);
    }

    y_intercept = ((y * xx) - (x * xy)) / ((capacity * xx) - (x * x));
    slope = ((capacity * xy) - (x * y)) / ((capacity * xx) - (x * x));
    printf("Regression Equation: temp = %ld.%03u + light * %ld.%03u\n", extractInteger(y_intercept), extractFraction(y_intercept), extractInteger(slope), extractFraction(slope));

    for(i=0; i < capacity; i++) {
        float e = (y_intercept + (lightDao.el[i] * slope)) - tempDao.el[i];
        sumofSquaredError += (e * e);
    }
    mse = sumofSquaredError / capacity;
    printf("Mean Squared Error = %ld.%03u \n\n", extractInteger(mse), extractFraction(mse));
}

/*
 * Implementation of Sensors
 * Relevant conversion functions for skymote
 */

// Transfer function for reading light sensor
float getLight(void) {
    float V_sensor = 1.5 * light_sensor.value(LIGHT_SENSOR_PHOTOSYNTHETIC)/4096;
    float I = V_sensor/100000;
    float light_lx = 0.625*1e6*I*1000;
    return light_lx;
}

// Transfer function for reading temperature sensor
float getTemperature(void) {
    int   tempADC = sht11_sensor.value(SHT11_SENSOR_TEMP_SKYSIM);
    float temp = 0.04*tempADC-39.6;
    return temp;
}

/* ===========================================================
                           Execution
 ============================================================= */
PROCESS(regression, "Regression");
AUTOSTART_PROCESSES(&regression);
PROCESS_THREAD(regression, ev, data) {
    static struct etimer timer;
    PROCESS_BEGIN();

    etimer_set(&timer, CLOCK_CONF_SECOND / MEASUREMENTS_PER_SECOND);

    SENSORS_ACTIVATE(light_sensor);
    SENSORS_ACTIVATE(sht11_sensor);

    while(1) {
        PROCESS_WAIT_EVENT_UNTIL(ev=PROCESS_EVENT_TIMER);

        float light_lx = getLight();
        float temp = getTemperature();
        float activity;
        queueLightMeasurement(light_lx);
        queueTempMeasurement(temp);
        printElements(lightDao, 'L');
        printElements(tempDao, 'T');
        // Start aggregating the data only after 12 readings are collected
        // K = 1; Aggregation is performed on each element being added to the FIFO queue
        if ((lightDao.size + 1) >= lightDao.capacity) {
            activity = calculateStandardDeviation(lightDao);
            printf("Light Readings StdDev = %ld.%03u\n", extractInteger(activity), extractFraction(activity));
            // Perform aggregation based on activity level
            if (activity <= LOW_ACTIVITY_THRESHOLD) {
                printLowActivityResults(lightDao);
            } else if (activity > HIGH_ACTIVITY_THRESHOLD) {
                printHighActivityResults(lightDao);
            } else {
                printMediumActivityResults(lightDao);
            }
            // Performs regression analysis on light and temperature readings
            calculateRegressionBetweenLightAndTemperature(lightDao, tempDao);
        }
        etimer_reset(&timer);
    }
    PROCESS_END();
}
