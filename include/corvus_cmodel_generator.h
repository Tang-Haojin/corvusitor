#ifndef CORVUS_CMODEL_GENERATOR_H
#define CORVUS_CMODEL_GENERATOR_H

#include "code_generator.h"
#include "connection_analysis.h"
#include <string>

// CorvusCModelGenerator: wraps CorvusGenerator output and emits a CModel entry.
class CorvusCModelGenerator : public CodeGenerator::TargetGenerator {
public:
  bool generate(const ConnectionAnalysis& analysis,
                const std::string& output_base,
                int mbus_count,
                int sbus_count) override;
};

#endif // CORVUS_CMODEL_GENERATOR_H
