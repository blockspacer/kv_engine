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
#ifndef SRC_CONFIGURATION_H_
#define SRC_CONFIGURATION_H_ 1

#include "config.h"

#include <memcached/engine.h>

#include <iostream>
#include <map>
#include <mutex>
#include <string>

#include "utility.h"

/**
 * The value changed listeners runs _without_ the global mutex for
 * the configuration class, so you may access other configuration
 * members from the callback.
 * The callback is fired <b>after</b> the value is set, so if you
 * want to prevent the caller from setting specific values you should
 * use the ValueChangedValidator instead.
 */
class ValueChangedListener {
public:
    void valueChanged(const std::string& key, bool value) {
        booleanValueChanged(key, value);
    }

    void valueChanged(const std::string& key, size_t value) {
        sizeValueChanged(key, value);
    }

    void valueChanged(const std::string& key, ssize_t value) {
        ssizeValueChanged(key, value);
    }

    void valueChanged(const std::string& key, float value) {
        floatValueChanged(key, value);
    }

    void valueChanged(const std::string& key, std::string value) {
        stringValueChanged(key, value.c_str());
    }

    void valueChanged(const std::string& key, const char* value) {
        stringValueChanged(key, value);
    }

    /**
     * Callback if when a boolean configuration value changed
     * @param key the key who changed
     * @param value the new value for the key
     */
    virtual void booleanValueChanged(const std::string &key, bool) {
        LOG(EXTENSION_LOG_DEBUG, "Configuration error.. %s does not expect"
            " a boolean value", key.c_str());
    }

    /**
     * Callback if when a numeric configuration value changed
     * @param key the key who changed
     * @param value the new value for the key
     */
    virtual void sizeValueChanged(const std::string &key, size_t) {
        LOG(EXTENSION_LOG_DEBUG, "Configuration error.. %s does not expect"
            " a size value", key.c_str());
    }

    /**
     * Callback if when a numeric configuration value changed
     * @param key the key who changed
     * @param value the new value for the key
     */
    virtual void ssizeValueChanged(const std::string &key, ssize_t) {
        LOG(EXTENSION_LOG_DEBUG, "Configuration error.. %s does not expect"
            " a size value", key.c_str());
    }

    /**
     * Callback if when a floatingpoint configuration value changed
     * @param key the key who changed
     * @param value the new value for the key
     */
    virtual void floatValueChanged(const std::string &key, float) {
        LOG(EXTENSION_LOG_DEBUG, "Configuration error.. %s does not expect"
            " a floating point value", key.c_str());
    }
    /**
     * Callback if when a string configuration value changed
     * @param key the key who changed
     * @param value the new value for the key
     */
    virtual void stringValueChanged(const std::string &key, const char *) {
        LOG(EXTENSION_LOG_DEBUG, "Configuration error.. %s does not expect"
            " a string value", key.c_str());
    }

    virtual ~ValueChangedListener() { /* EMPTY */}
};

/**
 * The validator for the values runs with the mutex held
 * for the configuration class, so you can't try to access
 * any other configuration variables from the callback
 */
class ValueChangedValidator {
public:
    void validate(const std::string& key, bool value) {
        validateBool(key, value);
    }

    void validate(const std::string& key, size_t value) {
        validateSize(key, value);
    }

    void validate(const std::string& key, ssize_t value) {
        validateSSize(key, value);
    }

    void validate(const std::string& key, float value) {
        validateFloat(key, value);
    }

    void validate(const std::string& key, const char* value) {
        validateString(key, value);
    }
    void validate(const std::string& key, std::string value) {
        validateString(key, value.c_str());
    }

    /**
     * Validator for boolean values
     * @param key the key that is about to change
     * @param value the requested new value
     * @throws runtime_error if the validation failed
     */
    virtual void validateBool(const std::string& key, bool) {
        std::string error = "Configuration error.. " + key +
                            " does not take a boolean parameter";
        LOG(EXTENSION_LOG_DEBUG, "%s", error.c_str());
        throw std::runtime_error(error);
    }

    /**
     * Validator for a numeric value
     * @param key the key that is about to change
     * @param value the requested new value
     * @throws runtime_error if the validation failed
     */
    virtual void validateSize(const std::string& key, size_t) {
        std::string error = "Configuration error.. " + key +
                            " does not take a size_t parameter";
        LOG(EXTENSION_LOG_DEBUG, "%s", error.c_str());
        throw std::runtime_error(error);
    }

    /**
     * Validator for a signed numeric value
     * @param key the key that is about to change
     * @param value the requested new value
     * @throws runtime_error if the validation failed
     */
    virtual void validateSSize(const std::string& key, ssize_t) {
        std::string error = "Configuration error.. " + key +
                            " does not take a ssize_t parameter";
        LOG(EXTENSION_LOG_DEBUG, "%s", error.c_str());
        throw std::runtime_error(error);
    }

    /**
     * Validator for a floating point
     * @param key the key that is about to change
     * @param value the requested new value
     * @throws runtime_error if the validation failed
     */
    virtual void validateFloat(const std::string& key, float) {
        std::string error = "Configuration error.. " + key +
                            " does not take a float parameter";
        LOG(EXTENSION_LOG_DEBUG, "%s", error.c_str());
        throw std::runtime_error(error);
    }

    /**
     * Validator for a character string
     * @param key the key that is about to change
     * @param value the requested new value
     * @throws runtime_error if the validation failed
     */
    virtual void validateString(const std::string& key, const char*) {
        std::string error = "Configuration error.. " + key +
                            " does not take a string parameter";
        LOG(EXTENSION_LOG_DEBUG, "%s", error.c_str());
        throw std::runtime_error(error);
    }

    virtual ~ValueChangedValidator() { }
};

class Requirement;

/**
 * The configuration class represents and provides access to the
 * entire configuration of the server.
 */
class Configuration {
public:
    struct value_t;

    Configuration();
    ~Configuration();

    // Include the generated prototypes for the member functions
#include "generated_configuration.h" // NOLINT(*)

    /**
     * Parse a configuration string and set the local members
     *
     * @param str the string to parse
     * @param sapi pointer to the server API
     * @return true if success, false otherwise
     */
    bool parseConfiguration(const char *str, SERVER_HANDLE_V1* sapi);

    /**
     * Add all of the configuration variables as stats
     * @param add_stat the callback to add statistics
     * @param c the cookie for the connection who wants the stats
     */
    void addStats(ADD_STAT add_stat, const void *c) const;

    /**
     * Add a listener for changes for a key. The configuration class
     * will release the memory for the ValueChangedListener by calling
     * delete in it's destructor (so you have to allocate it by using
     * new). There is no way to remove a ValueChangeListener.
     *
     * @param key the key to add the listener for
     * @param val the listener that will receive all of the callbacks
     *            when the value change.
     */
    void addValueChangedListener(const std::string &key,
                                 ValueChangedListener *val);

    /**
     * Set a validator for a specific key. The configuration class
     * will release the memory for the ValueChangedValidator by calling
     * delete in its destructor (so you have to allocate it by using
     * new). If a validator exists for the key, that will be returned
     * (and it's up to the caller to release the memory for that
     * validator).
     *
     * @param key the key to set the validator for
     * @param validator the new validator
     * @return the old validator (or NULL if there wasn't a validator)
     */
    ValueChangedValidator *setValueValidator(const std::string &key,
                                             ValueChangedValidator *validator);
    /**
     * Adds an alias for a configuration. Values can be set in configuration
     * under the original or aliased named, but setters/getters will only be
     * generated for the main name.
     *
     * @param key the key to which the alias refers
     * @param alias the new alias
     */
    void addAlias(const std::string& key, const std::string& alias);

    /**
     * Adds a prerequisite to a configuration option. This must be satisfied
     * in order to set/get the config value or for it to appear in stats.
     *
     * @param key the key to set the requirement for
     * @param requirement the requirement
     */
    Requirement* setRequirements(const std::string& key,
                                 Requirement* requirement);

    bool requirementsMet(const value_t& value) const;

    void requirementsMetOrThrow(const std::string& key) const;

protected:
    /**
     * Set the configuration parameter for a given key to
     * a new value (size_t, ssize_t, float, bool, string)
     * @param key the key to specify
     * @param value the new value
     * @throws runtime_error if the validation failed
     */
    template <class T>
    void setParameter(const std::string& key, T value);

    /**
     * Get the configuration parameter for a given key
     * @param key the key to specify
     * @return value the value
     * @throws runtime_error if the validation failed
     */
    template <class T>
    T getParameter(const std::string& key) const;

private:
    void initialize();

    // Access to the configuration variables is protected by the mutex
    mutable std::mutex mutex;
    std::map<std::string, std::shared_ptr<value_t>> attributes;

    friend std::ostream& operator<< (std::ostream& out,
                                     const Configuration &config);
};

// This specialisation is needed to convert char* to std::string to store in
// the variant.
template <>
void Configuration::setParameter<const char*>(const std::string& key,
                                              const char* value);

#endif  // SRC_CONFIGURATION_H_
