# Parasites OLIVERB / RESONESTOR, see sources.mk (-iquote for these two files only).
set(TP_PARASITES ${REPO_ROOT}/engines/thirdparty/parasites/floater_parasites.cc ${REPO_ROOT}/engines/thirdparty/parasites/floater_parasites_resources.cc)
set_source_files_properties(${TP_PARASITES} PROPERTIES COMPILE_OPTIONS "-iquote;${REPO_ROOT}/engines/thirdparty/parasites")
list(APPEND ENGINE_SOURCES ${TP_PARASITES})
