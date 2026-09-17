# Drumify: Real-Time Transient Analyzer & Parametric Drum Classifier

<img width="1194" height="696" alt="DrumifyUI" src="https://github.com/user-attachments/assets/cd1063b0-4a82-4fbe-9464-78e727062185" />



Have you ever wanted to translate musical ideas straight from your head into the world? Do you wish there were an intuitive, highly interpretable, and customizable way to do so? 

Welcome to **Drumify**—a plugin designed to transform vocal beatboxing into drum loops using your own customizable samples, as well as enable real-time drum replacement on existing audio tracks. 

While the initial concept and early foundations of this project were born out of a collaborative effort with a group of friends, I have since completely overhauled the architectural design, rewritten the core DSP pipelines, and engineered the statistical classification engine as the sole contributor. The project continues to be developed with the belief that the human voice is our most intuitive musical tool, especially for creators looking for a friction-free workflow.

---

## Technical Overview

Drumify is a lightweight, real-time audio analysis and transient classification system built in C++ using the **JUCE** framework. 

This project implements a hybrid approach, combining **classical digital signal processing (DSP)** for low-latency feature extraction on the audio thread with **parametric statistical machine learning** for offline drum classification. By avoiding heavy deep-learning neural networks, the system achieves negligible CPU overhead, making it highly suitable for integration into real-time audio plugins (VST3/AU) or standalone desktop applications.

---

## Key Features

### 1. Real-Time safe Onset Detection
* **Complex-Domain Spectral Difference:** Tracks instantaneous changes in both magnitude and phase velocity, offering highly sensitive transient detection.
* **Spectral Width Weighting:** Minimizes false positives from narrow-band noise (like microphone handling or guitar bleed) by dynamically scaling the onset detection function (ODF) based on the proportion of active frequency bands.
* **Dual-Moving-Average Statistical Tracker:** Employs fast (50-sample) and slow (1000-sample) simple moving averages (SMAs) with double-precision running variance tracking to dynamically adjust detection thresholds.

### 2. Latency-Compensated Buffering
* **Retrospective Recording:** Because spectral analysis and statistical confirmation introduce roughly 1500 samples of latency, the system utilizes a **4096-sample circular pre-roll buffer** to capture the absolute beginning (including a silent cushion) of the physical transient once confirmed.
* **Offset & Decay Detection:** Monitors the real-time amplitude envelope to cleanly close and finalize recorded hit buffers when the signal drops below the noise floor.

### 3. Feature Extraction & Temporal Pooling
To capture the dynamic, time-varying nature of drum hits, the system analyzes the first 10 windows (approx. 58ms) of the hit and extracts several physical features:
* **Mean Centroid:** Evaluates overall brightness using a Mel-filterbank center-of-mass calculation.
* **Least-Squares Linear Regression Delta:** Fits a trend line across all transient frames to evaluate pitch/brightness travel (e.g. tracking how a kick's pitch slides downwards).
* **Spectral Peak Prominence:** Scans the $100\text{ Hz} - 800\text{ Hz}$ band to detect sharp, narrow sinusoidal resonance (shell ring) to differentiate snare drums from cymbals.
* **Temporal Centroid Decay:** Calculates the time-domain center-of-mass of the low and high frequency envelopes independently to evaluate which frequencies ring out longer.

### 4. Gaussian Naive Bayes Classifier
* Classifies hits into **Kicks, Snares, or Hi-Hats** using a supervised, parametric probabilistic model.
* Evaluates the multi-dimensional Gaussian probability density function (PDF) for each class and normalizes the results into intuitive confidence percentages.
* Employs power-weighting ($p^w$) to allow different features to hold more or less statistical influence depending on the drum type being evaluated.

---

## File Architecture

* **`InputProcessor`**: Manages the real-time/offline sample loop, circular pre-roll buffer, and the active recording state machine.
* **`ComplexOdf`**: Computes the complex-domain spectral difference.
* **`StatisticalOnsetDetector`**: Manages the dual-moving averages and calculates running variance and standard deviation.
* **`DrumFeatureExtractor`**: Houses static helper functions for extracting spectral centroids, low-mid peak prominence, and temporal centroids.
* **`ProbabilisticDrumClassifier`**: Defines the parametric means/standard deviations and implements the Gaussian Naive Bayes logic.

---

## How to Build (Using Projucer)

This project is configured as a JUCE application and is generated using the **Projucer**.

### Prerequisites
1. Download and install the [JUCE Framework](https://juce.com/download/).
2. An IDE of your choice: Xcode (macOS) or Visual Studio (Windows).

### Build Steps
1. Open the Projucer application.
2. Select **Open Existing Project...** and navigate to your `.jucer` file in the project directory.
3. Verify your module paths under the **Modules** tab on the left (ensure `juce_audio_basics`, `juce_audio_formats`, and `juce_dsp` are successfully located).
4. Select your preferred exporter target (e.g., *Xcode (macOS)* or *Visual Studio 2022 (Windows)*).
5. Click the **Save Project and Open in IDE** button (the Xcode/Visual Studio icon at the top of the Projucer).
6. Once your IDE opens, select the **Standalone** or **Plugin** target and click **Build / Run**.

---

## Dynamic Signal Flow

```text
[Input Audio Buffer] 
        │
        ├──> [4096-Sample Circular Pre-Roll] (Stored for retrospective capture)
        │
        └──> [FFT Overlapping Sliding Window] (Window: 1024, Hop: 256)
                    │
                    ├──> [Complex ODF Spectral Difference]
                    │           │
                    │           └──> [Spectral Width Weighting]
                    │                       │
                    │                       └──> [Statistical Onset Detector (SMAs)]
                    │                                    │
                    │                                    └──> ONSET CONFIRMED?
                    │                                              │
                    │  ┌───────────────────────────────────────────┘
                    │  ▼
         [Retrospective Copying] (Pulls past ~1756 samples from Pre-Roll)
                    │
         [Sample-by-Sample Recording] (Appends live samples until offset decay)
                    │
         [Finalize Hit Buffer] (Triggers when signal falls below threshold)
                    │
         [Offline Feature Extractor] (Centroids, Delta, Prominence, Decay)
                    │
         [Gaussian Naive Bayes] (Probability Calculation & Normalization)
                    │
         [Playback] (Can be heard by user)
```
