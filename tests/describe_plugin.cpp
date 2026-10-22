// Prints every module of a Rack plugin as JSON: ports, params and its default dataToJson().
// Used by tests/gui_shots.py to build live patches. Test-only.
//   build/describe_plugin <plugin dir containing plugin.so>
#include <rack.hpp>

#include <cstdio>
#include <dlfcn.h>
#include <string>

using namespace rack;

void smokeMakeEngine(); // tests/all_modules_engine.cpp

int main(int argc, char** argv) {
	if (argc < 2) return 2;
	smokeMakeEngine();
	std::string dir = argv[1];
	void* h = dlopen((dir + "/plugin.so").c_str(), RTLD_NOW | RTLD_LOCAL);
	if (!h) { std::fprintf(stderr, "%s\n", dlerror()); return 1; }
	auto initFn = (void (*)(Plugin*)) dlsym(h, "init");
	if (!initFn) return 1;
	Plugin* p = new Plugin;
	p->path = dir;
	initFn(p);
	json_t* all = json_array();
	for (Model* model : p->models) {
		Module* m = model->createModule();
		json_t* o = json_object();
		json_object_set_new(o, "slug", json_string(model->slug.c_str()));
		json_t* ins = json_array();
		for (auto* pi : m->inputInfos) json_array_append_new(ins, json_string(pi ? pi->name.c_str() : ""));
		json_object_set_new(o, "inputs", ins);
		json_t* outs = json_array();
		for (auto* pi : m->outputInfos) json_array_append_new(outs, json_string(pi ? pi->name.c_str() : ""));
		json_object_set_new(o, "outputs", outs);
		json_t* ps = json_array();
		for (auto* pq : m->paramQuantities) {
			json_t* q = json_object();
			json_object_set_new(q, "name", json_string(pq ? pq->name.c_str() : ""));
			json_object_set_new(q, "min", json_real(pq ? pq->minValue : 0));
			json_object_set_new(q, "max", json_real(pq ? pq->maxValue : 0));
			json_object_set_new(q, "default", json_real(pq ? pq->defaultValue : 0));
			json_array_append_new(ps, q);
		}
		json_object_set_new(o, "params", ps);
		json_t* d = m->dataToJson();
		json_object_set_new(o, "data", d ? d : json_null());
		json_array_append_new(all, o);
		delete m;
	}
	char* s = json_dumps(all, JSON_INDENT(1));
	std::puts(s);
	std::free(s);
	return 0;
}
