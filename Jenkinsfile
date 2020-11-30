#!/usr/bin/env groovy

@Library('etn-ipm2-jenkins@feature/disableConcurrentBuilds-option') _

//We want only release build and deploy in OBS
//We disabled debug build with tests
import params.ZprojectPipelineParams
ZprojectPipelineParams parameters = new ZprojectPipelineParams()
parameters.enableCoverity = false

etn_ipm2_build_and_tests_pipeline_zproject(parameters)
