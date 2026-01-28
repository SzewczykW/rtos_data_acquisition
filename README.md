# RTOS Data Acquisition

Data acquisition system based on the NXP LPC1768 microcontroller with CMSIS-RTOS2 (RTX5)
real-time operating system. The project implements remote ADC sample collection
and data transmission over Ethernet using the UDP protocol.


## Build and Run

### Firmware (Keil uVision)

1. Open project `data_acquisition.uvprojx` in Keil uVision
2. Build -> Rebuild All Target Files
3. Flash -> Download

### Software (Python)

```bash
pip install .
data-acquisition --help
```
---
### Documentation (Doxygen)

To generate documentation one needs to have following packages installed:
- Doxygen>=1.9.8
- Graphviz>=2.42.2

Then run the following command in the project root directory:

```bash
make docs
```

The documentation will be generated in the `docs/html/` directory. Open `index.html` in a web browser to view.
