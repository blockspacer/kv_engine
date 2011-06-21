/* -*- Mode: C++; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 *     Copyright 2011 Couchbase, Inc
 *
 *   Licensed under the Apache License, Version 2.0 (the "License");
 *   you may not use this file except in compliance with the License.
 *   You may obtain a copy of the License at
 *
 *       http://www.apache.org/licenses/LICENSE-2.0
 *
 *   Unless required by applicable law or agreed to in writing, software
 *   distributed under the License is distributed on an "AS IS" BASIS,
 *   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *   See the License for the specific language governing permissions and
 *   limitations under the License.
 */
#include <cassert>
#include <cstdio>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <cerrno>
#include <sys/stat.h>
#include <string>
#include <sstream>
#include <iostream>
#include <fstream>
#include <map>

#include <ctype.h>

#include "cJSON.h"

using namespace std;

stringstream prototypes;
stringstream initialization;
stringstream implementation;

typedef string (*getValidatorCode)(const std::string &, cJSON*);

std::map<string, getValidatorCode> validators;

std::ostream& operator <<(std::ostream &out, const cJSON *o) {
    switch (o->type) {
    case cJSON_Number:
        if (o->valueint != o->valuedouble) {
            out << (float)o->valuedouble;
        } else {
            out << o->valueint;
        }
        break;
    case cJSON_String:
        out << '"' << o->valuestring << '"';
        break;
    default:
        cerr << "Internal error.. unknow json code" << endl;
        abort();
    }
    return out;
}

bool isFloat(const cJSON *o) {
    return o->valueint != o->valuedouble;
}

string getRangeValidatorCode(const std::string &key, cJSON *o) {
    // the range validator should contain a "min" and "max" element
    cJSON *min = cJSON_GetObjectItem(o, "min");
    cJSON *max = cJSON_GetObjectItem(o, "max");

    if (min == 0 || max == 0) {
        cerr << "Incorrect syntax for a range validator specified for"
             << "\"" << key << "\"." << endl
             <<"You need both a min and max clause." << endl;
        exit(1);
    }

    if (min->type != max->type || min->type != cJSON_Number) {
        cerr << "Incorrect datatype for the range validator specified for "
             << "\"" << key << "\"." << endl
             << "Only numbers are supported." << endl;
        exit(1);
    }

    stringstream ss;
    if (isFloat(min) || isFloat(max)) {
        ss << "(new FloatRangeValidator())->min((float)" << min << ")->max((float)" << max << ")";
    } else {
        ss << "(new SizeRangeValidator())->min(" << min << ")->max(" << max << ")";
    }

    return ss.str();
}

static void initialize() {
    prototypes << "// ###########################################" << endl
               << "// # DO NOT EDIT! THIS IS A GENERATED FILE " << endl
               << "// ###########################################" << endl;

    implementation << "// ###########################################" << endl
                   << "// # DO NOT EDIT! THIS IS A GENERATED FILE " << endl
                   << "// ###########################################" << endl;
    validators["range"] = getRangeValidatorCode;
}

static string getString(cJSON *i) {
    if (i == NULL) {
        return "";
    }
    assert(i->type == cJSON_String);
    return i->valuestring;
}

static bool isReadOnly(cJSON *o) {
    cJSON *i = cJSON_GetObjectItem(o, "dynamic");
    if (i == NULL || i->type == cJSON_False) {
        return false;
    }

    assert(i->type == cJSON_True);
    return true;
}

static string getDatatype(cJSON *o) {
    cJSON *i = cJSON_GetObjectItem(o, "type");
    assert(i != NULL && i->type == cJSON_String);
    string ret = i->valuestring;

    if (ret.compare("bool") == 0 ||
        ret.compare("size_t") == 0 ||
        ret.compare("float") == 0) {
        return ret;
    } else if (ret.compare("string") == 0 ||
               ret.compare("std::string") == 0) {
        return "std::string";
    } else {
        cerr << "Invalid datatype: " << ret;
        abort();
    }
}

static string getValidator(const std::string &key, cJSON *o) {
    if (o == NULL) {
        return "";
    }

    cJSON *n = cJSON_GetArrayItem(o, 0);
    if (n == NULL) {
        return "";
    }

    std::map<string, getValidatorCode>::iterator iter;
    iter = validators.find(string(n->string));
    if (iter == validators.end()) {
        cerr << "Unknown validator specified for \"" << key
             << "\": \"" << n->string << "\""
             << endl;
        exit(1);
    }

    return (iter->second)(key, n);
}

static string getGetterPrefix(const string &str) {
    if (str.compare("bool") == 0) {
        return "is";
    } else {
        return "get";
    }
}

static string getCppName(const string &str) {
    stringstream ss;
    bool doUpper = true;

    string::const_iterator iter;
    for (iter = str.begin(); iter != str.end(); ++iter) {
        if (*iter == '_') {
            doUpper = true;
        } else {
            if (doUpper) {
                ss << (char)toupper(*iter);
                doUpper = false;
            } else {
                ss << (char)*iter;
            }
        }
    }
    return ss.str();
}

static string getInternalGetter(const std::string &type) {
    if (type.compare("std::string") == 0) {
        return "getString";
    } else if (type.compare("bool") == 0) {
        return "getBool";
    } else if (type.compare("size_t") == 0) {
        return "getInteger";
    } else if (type.compare("float") == 0) {
        return "getFloat";
    } else {
        cerr << "Unsupported datatype: " << type << endl;
        abort();
    }
}

static void generate(cJSON *o) {
    assert(o != NULL);

    string config_name = o->string;
    string cppname = getCppName(config_name);
    string type = getDatatype(o);
    string defaultVal = getString(cJSON_GetObjectItem(o, "default"));
    string validator = getValidator(config_name,
                                    cJSON_GetObjectItem(o, "validator"));

    // Generate prototypes
    prototypes << "    " << type
               << " " << getGetterPrefix(type)
               << cppname << "() const;" << endl;
    if  (!isReadOnly(o)) {
        prototypes << "    void set" << cppname << "(const " << type
                   << " &nval);" << endl;
    }

    // Generate initialization code
    initialization << "    setParameter(\"" << config_name << "\", ";
    if (type.compare("std::string") == 0) {
        initialization << "(const char*)\"" << defaultVal << "\");" << endl;
    } else {
        initialization << "(" << type << ")" << defaultVal << ");" << endl;
    }
    if (!validator.empty()) {
        initialization << "    setValueValidator(\"" << config_name
                       << "\", " << validator << ");" << endl;
    }


    // Generate the getter
    implementation << type << " Configuration::" << getGetterPrefix(type)
                   << cppname << "() const {" << endl
                   << "    return " << getInternalGetter(type) << "(\""
                   << config_name << "\");" << endl << "}" << endl;

    if  (!isReadOnly(o)) {
        // generate the setter
        implementation << "void Configuration::set" << cppname
                       << "(const " << type << " &nval) {" << endl
                       << "    setParameter(\"" << config_name
                       << "\", nval);" << endl
                       << "}" << endl;
    }
}

/**
 * Read "configuration.json" and generate getters and setters
 * for the parameters in there
 */
int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    initialize();

    struct stat st;
    if (stat("configuration.json", &st) == -1) {
        cerr << "Failed to look up configuration.json: "
             << strerror(errno) << endl;
        return 1;
    }

    char *data = new char[st.st_size + 1];
    data[st.st_size] = 0;
    ifstream input("configuration.json");
    input.read(data, st.st_size);
    input.close();

    cJSON *c = cJSON_Parse(data);
    if (c == NULL) {
        cerr << "Failed to parse JSON.. probably syntax error" << endl;
        return 1;
    }

    cJSON *params = cJSON_GetObjectItem(c, "params");
    if (params == NULL) {
        cerr << "FATAL: could not find \"params\" section" << endl;
        return 1;
    }

    int num = cJSON_GetArraySize(params);
    for (int ii = 0; ii < num; ++ii) {
        generate(cJSON_GetArrayItem(params, ii));
    }

    ofstream headerfile("generated_configuration.hh");
    headerfile << prototypes.str();
    headerfile.close();

    ofstream implfile("generated_configuration.cc");
    implfile << implementation.str() << endl
             << "void Configuration::initialize() {" << endl
             << initialization.str()
             << "}" << endl;
    implfile.close();

    cJSON_Delete(c);
    delete []data;

    return 0;
}
