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

long extractInteger(float f) {
    return ((long) f);
}

unsigned int extractFraction(float f) {
    int fractionPart = (int) 1000 * (f - extractInteger(f));
    return (abs(fractionPart));
}

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

struct FIFOQueue {
    unsigned int capacity;
    int size;
    int last;
    float el[12];
};

struct FIFOQueue lightDao = {
        12,
        0,
        11,
        { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }
};

void queueLightMeasurement(float item) {
    int x;
    for (x=(lightDao.last - 1); x >= 0; x--) {
        lightDao.el[x + 1] = lightDao.el[x];
    }
    lightDao.el[0] = item;
    if (lightDao.size < (lightDao.capacity - 1))
        lightDao.size = lightDao.size + 1;
}

void printElements(struct FIFOQueue dao) {
    int i;
    printf("\nB = [");
    for (i=0; i <= dao.last; i++){
        printf("%ld.%03u", extractInteger(dao.el[i]), extractFraction(dao.el[i]));
        if (i != dao.last) {
            printf(", ");
        }
    }
    printf("]\n");
}

void printHighActivityResults(struct FIFOQueue dao) {
    int i;
    printf("Aggregation = None [ High Activity ]\n");
    printf("X = [");
    for (i=0; i <= dao.last; i++){
        printf("%ld.%03u", extractInteger(dao.el[i]), extractFraction(dao.el[i]));
        if (i != dao.last) {
            printf(", ");
        }
    }
    printf("]\n");
}

void printMediumActivityResults(struct FIFOQueue dao) {
    float results[3];
    results[0] = (dao.el[0] + dao.el[1] + dao.el[2] + dao.el[3]) / 4.0;
    results[1] = (dao.el[4] + dao.el[5] + dao.el[6] + dao.el[7]) / 4.0;
    results[2] = (dao.el[8] + dao.el[9] + dao.el[10] + dao.el[11]) / 4.0;

    int i;
    printf("Aggregation = 4-into-1 [ Medium Activity ]\n");
    printf("X = [");
    for (i=0; i < 3; i++){
        printf("%ld.%03u", extractInteger(results[i]), extractFraction(results[i]));
        if (i != 2) {
            printf(", ");
        }
    }
    printf("]\n");
}

void printLowActivityResults(struct FIFOQueue dao) {
    int i;
    float sum = 0.0;
    for (i = 0; i < dao.capacity; ++i) {
        sum += dao.el[i];
    }
    float result = sum / (float)dao.capacity;
    printf("Aggregation = 12-into-1 [ Low Activity ]\n");
    printf("X = [ %ld.%03u ]\n", extractInteger(result), extractFraction(result));
}

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

/*
 * Implementation of Sensors
 * Relevant conversion functions for skymote
 */
float getLight(void) {
    float V_sensor = 1.5 * light_sensor.value(LIGHT_SENSOR_PHOTOSYNTHETIC)/4096;
    float I = V_sensor/100000;
    float light_lx = 0.625*1e6*I*1000;
    return light_lx;
}

/* ===========================================================
                           Execution
 ============================================================= */
PROCESS(correlation, "Correlation");
AUTOSTART_PROCESSES(&correlation);
PROCESS_THREAD(correlation, ev, data) {
    static struct etimer timer;
    PROCESS_BEGIN();

    etimer_set(&timer, CLOCK_CONF_SECOND / MEASUREMENTS_PER_SECOND);

    SENSORS_ACTIVATE(light_sensor);
    SENSORS_ACTIVATE(sht11_sensor);

    while(1) {
        PROCESS_WAIT_EVENT_UNTIL(ev=PROCESS_EVENT_TIMER);

        float light_lx = getLight();
        float activity;
        queueLightMeasurement(light_lx);
        printElements(lightDao);
        if ((lightDao.size + 1) >= lightDao.capacity) {
            activity = calculateStandardDeviation(lightDao);
            printf("StdDev = %ld.%03u\n", extractInteger(activity), extractFraction(activity));
            if (activity <= LOW_ACTIVITY_THRESHOLD) {
                printLowActivityResults(lightDao);
            } else if (activity > HIGH_ACTIVITY_THRESHOLD) {
                printHighActivityResults(lightDao);
            } else {
                printMediumActivityResults(lightDao);
            }
        }
        etimer_reset(&timer);
    }
    PROCESS_END();
}
