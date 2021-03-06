#ifndef __ARC_JOBSTATE_H__
#define __ARC_JOBSTATE_H__

#include <string>
#include <map>

#ifdef JOBSTATE_TABLE
#undef JOBSTATE_TABLE
#endif

#ifdef JOBSTATE_X
#undef JOBSTATE_X
#endif

namespace Arc {

  /** libarccompute state model.
   * The class comprise the general state model used by the libarccompute
   * library. A JobState object has two attributes: A native state as a string,
   * and a enum value specifying the mapping to the state model. Each job
   * management extension (JobControllerPlugin specialisation), likely a
   * implementation against a computing service, should define a mapping of the
   * native job states to those in the libarccompute state model, which should
   * then be used when constructing a JobState object for that specific
   * extension. In that way both the general and the specific state is
   * available.
   * 
   * A derived class should consist of a constructor and a mapping function (a
   * JobStateMap) which maps a
   * std::string to a JobState:StateType. An example of a constructor in a
   * plugin could be:
   * JobStatePlugin::JobStatePluging(const std::string& state) : JobState(state, &pluginStateMap) {}
   * where &pluginStateMap is a reference to the JobStateMap defined by the
   * derived class.
   * 
   * Documentation for mapping of job states for different computing services to
   * those defined in this class can be found \subpage job_state_mapping "here".
   * 
   * \ingroup compute
   * \headerfile JobState.h arc/compute/JobState.h 
   */
  class JobState {
  public:
    /** \enum StateType
     * \brief Possible job states in libarccompute
     * 
     * The possible job states usable in the libarccompute library with a short
     * description is listed below:
     * 
     * \mapdef job_state_mapping Job state mapping
     * On this page the mapping of job state attributes of different
     * computing services to those defined by the libarccompute library in the
     * \ref Arc::JobState "JobState" class is documented.
     **/
    enum StateType {
      /** %Job state could not be resolved.
       * \mapdefattr UNDEFINED Arc::JobState
       **/
      UNDEFINED,
      
      /** %Job was accepted by the computing service.
       * \mapdefattr ACCEPTED Arc::JobState
       */
      ACCEPTED,
      
      /** %Job is being prepared by the computing service.
       * \mapdefattr PREPARING Arc::JobState
       **/
      PREPARING,

      /** %Job is being submitted to a computing share.
       * \mapdefattr SUBMITTING Arc::JobState
       **/
      SUBMITTING,
      
      /** %Job is put on hold.
       * \mapdefattr HOLD Arc::JobState
       **/
      HOLD,
      
      /** %Job is on computing share waiting to run.
       * \mapdefattr QUEUING Arc::JobState
       **/
      QUEUING,
      
      /** %Job is running on computing share.
       * \mapdefattr RUNNING Arc::JobState
       **/
      RUNNING,
      
      /** %Job is finishing.
       * \mapdefattr FINISHING Arc::JobState
       **/
      FINISHING,
      
      /** %Job has finished.
       * \mapdefattr FINISHED Arc::JobState
       **/
      FINISHED,
      
      /** %Job has been killed.
       * \mapdefattr KILLED Arc::JobState
       **/
      KILLED,
      
      /** %Job failed.
       * \mapdefattr FAILED Arc::JobState
       **/
      FAILED,
      
      /** %Job have been deleted.
       * \mapdefattr DELETED Arc::JobState
       **/
      DELETED,
      
      /** Any job state which does not fit the above states.
       * \mapdefattr OTHER Arc::JobState
       **/
      OTHER
    };

    static const std::string StateTypeString[];

    JobState() : ssf(FormatSpecificState), type(UNDEFINED) {}
    /// Construct state from string
    /**
     * \since Added in 4.0.0.
     **/
    JobState(const std::string& jobstate) : ssf(FormatSpecificState), state(jobstate), type(GetStateType(jobstate)) {}
    
    JobState& operator=(const JobState& js) { type = js.type; state = js.state; ssf = js.ssf; return *this; }

    operator bool() const { return type != UNDEFINED; }
    operator StateType() const { return type; }
    bool operator!() const { return type == UNDEFINED; }
    bool operator==(const StateType& st) const { return type == st; }
    bool operator!=(const StateType& st) const { return type != st; }

    /// Check if state is finished
    /**
     * @return true is returned if the StateType is equal to FINISHED, KILLED,
     *  FAILED or DELETED, otherwise false is returned.
     **/
    bool IsFinished() const { return type == FINISHED || type == KILLED || type == FAILED || type == DELETED; }

    /// Unformatted specific job state
    /**
     * Get the unformatted specific job state as returned by the CE.
     * 
     * @return job state as returned by CE
     * @see GetSpecificState
     * @see GetGeneralState
     **/
    const std::string& operator()() const { return state; }

    /// General string representation of job state
    /**
     * Get the string representation of the job state as mapped to the
     * libarccompute job state model.
     * 
     * @return string representing general job state
     * @see enum StateType
     * @see GetSpecificState
     **/
    const std::string& GetGeneralState() const { return StateTypeString[type]; }
    
    /// Specific string representation of job state
    /**
     * Get the string representation of the job state as returned by
     * the CE service possibly formatted to a human readable string.
     * 
     * @return string representing specific, possibly formatted, job state
     * @see GetGeneralState
     * @see operator()
     **/
    std::string GetSpecificState() const { return ssf(state); }

    static StateType GetStateType(const std::string& state);

    friend class Job;

  protected:
    typedef std::string (*SpecificStateFormater)(const std::string&);
    SpecificStateFormater ssf;
    static std::string FormatSpecificState(const std::string& state) { return state; }
    
    JobState(const std::string& state, JobState::StateType (*map)(const std::string&), SpecificStateFormater ssf = FormatSpecificState)
      : ssf(ssf), state(state), type((*map)(state)) {};
    std::string state;
    StateType type;
  };

  /**
   * \ingroup compute
   * \headerfile JobState.h arc/compute/JobState.h 
   */
  typedef JobState::StateType (*JobStateMap)(const std::string&);
}

#endif // __ARC_JOBSTATE_H__
