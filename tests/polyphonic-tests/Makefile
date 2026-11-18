CXX ?= c++
CXXFLAGS ?= -O2 -std=c++14
CPPFLAGS ?= -I../../architecture
FAUST ?= faust

.PHONY: check clean

check: voice-steal-repro legato-fade-repro bargraph-current-repro mcu-poly
	./voice-steal-repro
	./legato-fade-repro
	./bargraph-current-repro

voice-steal-repro: voice-steal-repro.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< -o $@

legato-fade-repro: legato-fade-repro.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< -o $@

bargraph-current-repro: bargraph-current-repro.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< -o $@

# mcu-poly-dsp.h against poly-dsp.h (UI paths, MIDI, assign modes, bit identical audio)
mcu-poly: mcu-poly-test
	./mcu-poly-test

mcupolyvoice.h: mcu-poly-voice.dsp
	$(FAUST) -cn mcupolyvoice -i $< -o $@

mcupolyeffect.h: mcu-poly-voice.dsp
	$(FAUST) -cn mcupolyeffect -pn effect -i $< -o $@

mcu-poly-test: mcu-poly-test.cpp mcupolyvoice.h mcupolyeffect.h ../../architecture/faust/dsp/mcu-poly-dsp.h
	$(CXX) $(CPPFLAGS) -I. $(CXXFLAGS) $< -o $@

clean:
	rm -f voice-steal-repro legato-fade-repro bargraph-current-repro mcu-poly-test mcupolyvoice.h mcupolyeffect.h
