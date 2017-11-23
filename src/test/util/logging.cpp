#include "logging.h"

#include <glog/logging.h>

void util::SetupLogging(const char *program_name) {
    FLAGS_logtostderr = true;
    FLAGS_colorlogtostderr = true;
    FLAGS_minloglevel = google::INFO;

    google::InitGoogleLogging(program_name);
    google::InstallFailureSignalHandler();
}