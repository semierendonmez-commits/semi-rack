# Parasites OLIVERB / RESONESTOR (Matthias Puech on Emilie Gillet's Clouds, MIT), FLOATER's wrapper
# (floater_parasites.*). Its own copies of Clouds' headers must win over the Move port's for these two
# files only: -iquote puts engines/thirdparty/parasites ahead of every -I for "clouds/..." includes.
PARASITES_OBJS := build/engines/thirdparty/parasites/floater_parasites.cc.o build/engines/thirdparty/parasites/floater_parasites_resources.cc.o
$(PARASITES_OBJS): FLAGS += -iquote engines/thirdparty/parasites
ENGINE_SOURCES += engines/thirdparty/parasites/floater_parasites.cc engines/thirdparty/parasites/floater_parasites_resources.cc
