//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#ifndef PXR_BASE_TF_DIAGNOSTIC_MGR_H
#define PXR_BASE_TF_DIAGNOSTIC_MGR_H

/// \file tf/diagnosticMgr.h

#include "pxr/pxr.h"
#include "pxr/base/tf/callContext.h"
#include "pxr/base/tf/debug.h"
#include "pxr/base/tf/diagnosticContainer.h"
#include "pxr/base/tf/diagnosticLite.h"
#include "pxr/base/tf/error.h"
#include "pxr/base/tf/singleton.h"
#include "pxr/base/tf/smallVector.h"
#include "pxr/base/tf/spinRWMutex.h"
#include "pxr/base/tf/status.h"
#include "pxr/base/tf/stringUtils.h"
#include "pxr/base/tf/warning.h"
#include "pxr/base/tf/weakPtr.h"
#include "pxr/base/tf/enum.h"
#include "pxr/base/tf/api.h"

#include "pxr/base/arch/align.h"
#include "pxr/base/arch/inttypes.h"
#include "pxr/base/arch/attributes.h"
#include "pxr/base/arch/functionLite.h"

#include <tbb/enumerable_thread_specific.h>

#include <atomic>
#include <cstdarg>
#include <list>
#include <memory>
#include <string>
#include <vector>

PXR_NAMESPACE_OPEN_SCOPE

TF_DEBUG_CODES(
    TF_LOG_STACK_TRACE_ON_ERROR,
    TF_LOG_STACK_TRACE_ON_WARNING,
    TF_ERROR_MARK_TRACKING,
    TF_PRINT_ALL_POSTED_ERRORS_TO_STDERR
    );

class TfDiagnosticTrap;
class TfError;
class TfErrorMark;

/// \class TfDiagnosticMgr
/// \ingroup group_tf_Diagnostic
///
/// Singleton class through which all errors and diagnostics pass.
class TfDiagnosticMgr : public TfWeakBase {
public:

    typedef TfDiagnosticMgr This;

    typedef std::list<TfError> ErrorList;

    /// Synonym for standard STL iterator to traverse the error list.
    ///
    /// The error list for a thread is an STL list.  The \c ErrorIterator type
    /// is an STL iterator and can be used without restriction in any way that
    /// it is legal to use an STL iterator.
    ///
    /// Given an iterator, one accesses the error in the standard STL fashion:
    /// \code
    ///     TfErrorMark m;
    ///
    ///     ... ;
    ///     if (!m.IsClean()) {
    ///         TfErrorMark::Iterator i;
    ///         for (i = m.GetBegin(); i != m.GetEnd(); ++i) {
    ///            cout << "file = " << i->GetSourceFileName()
    ///                 << "line = " << i->GetSourceLineNumber() << "\n";
    ///         }
    /// \endcode
    typedef ErrorList::iterator ErrorIterator;

    /// Returns the name of the given diagnostic code.
    TF_API
    static std::string GetCodeName(const TfEnum &code);

    /// Return a human-readable diagnostic message. The TfDiagnosticMgr uses 
    /// this function to print diagnostics when no diagnostic delegates are 
    /// installed. Diagnostic delegate implementations can call this to produce 
    /// messages in the same format, if desired.
    TF_API
    static std::string FormatDiagnostic(const TfEnum &code, 
            const TfCallContext &context, const std::string &msg, 
            const TfDiagnosticInfo &info);

    /// \class Delegate
    /// One may set a delegate with the \c TfDiagnosticMgr which will be
    /// called to respond to errors and diagnostics.
    ///
    /// \note None of the methods in \c TfDiagnosticMgr::Delegate can be
    /// reentrant.
    ///
    /// Practically speaking, this means they cannot invoke:
    ///
    /// - TF_ERROR
    /// - TF_RUNTIME_ERROR
    /// - TF_CODING_ERROR
    /// - TF_WARN
    /// - TF_STATUS
    ///
    /// For a more complete list, see diagnostic.h 
    ///
    class Delegate {
      public:
        TF_API 
        virtual ~Delegate() = 0;

        /// Called when a \c TF_STATUS() is issued.
        virtual void IssueStatus(TfStatus const &status) = 0;

        /// Called when a \c TF_WARNING() is issued.
        virtual void IssueWarning(TfWarning const &warning) = 0;

        /// Called when a \c TfError is posted.
        virtual void IssueError(TfError const &err) = 0;

        /// Called when a \c TF_FATAL_ERROR is issued (or a failed
        /// \c TF_AXIOM).
        virtual void IssueFatalError(TfCallContext const &context,
                                     std::string const &msg) = 0;
    protected:
        /// Abort the program, but avoid the session logging mechanism. This
        /// is intended to be used for fatal error cases where any information
        /// has already been logged.
        TF_API
        void _UnhandledAbort() const;
    };

    /// Return the singleton instance.
    TF_API static This &GetInstance() {
        return TfSingleton<This>::GetInstance();
    }

    /// Add the delegate \p delegate to the list of current delegates.
    ///
    /// This will add the delegate even if it already exists in the list.
    ///
    /// Each delegate will be called when diagnostics and errors are invoked
    ///
    /// This function is thread safe.
    TF_API
    void AddDelegate(Delegate* delegate);

    /// Removes all delegates equal to \p delegate from the current delegates.
    ///
    /// This function is thread safe.
    TF_API
    void RemoveDelegate(Delegate* delegate);

    /// Set whether errors, warnings and status messages should be printed out
    /// to the terminal.
    TF_API
    void SetQuiet(bool quiet) { _quiet = quiet; }
    
    /// Return an iterator to the beginning of this thread's error list.
    ErrorIterator GetErrorBegin() { return _errorList.local().begin(); }

    /// Return an iterator to the end of this thread's error list.
    ErrorIterator GetErrorEnd() { return _errorList.local().end(); }

    /// Remove error specified by iterator \p i.
    /// \deprecated Use TfErrorMark instead.
    TF_API
    ErrorIterator EraseError(ErrorIterator i);

    /// Remove all the errors in [first, last) from this thread's error
    /// stream. This should generally not be invoked directly. Use TfErrorMark
    /// instead.
    TF_API
    ErrorIterator EraseRange(ErrorIterator first, ErrorIterator last);

    /// Append an error to the list of active errors.  This is generally not
    /// meant to be called by user code.  It is public so that the system
    /// which translates tf errors to and from python exceptions can manage
    /// errors.
    TF_API
    void AppendError(TfError const &e);
    
    /// This method will create a TfError, append it to the error list, and
    /// pass it to all delegates.
    ///
    /// If no delegates have been registered and no error mark is active, this
    /// method will print the error to stderr.
    TF_API
    void PostError(TfEnum errorCode, const char* errorCodeString,
        TfCallContext const &context,  
        const std::string& commentary, TfDiagnosticInfo info,
        bool quiet);
    
    /// This method will create a TfError, append it to the error list, and
    /// pass it to all delegates.
    ///
    /// If no delegates have been registered and no error mark is active, this
    /// method will print the error to stderr.
    TF_API
    void PostError(const TfDiagnosticBase& diagnostic);

    /// This method will create a TfWarning and pass it to all delegates.
    ///
    /// If no delegates have been registered, this method will print the
    /// warning msg to stderr.
    TF_API
    void PostWarning(TfEnum warningCode, const char *warningCodeString,
        TfCallContext const &context, std::string const &commentary,
        TfDiagnosticInfo info, bool quiet);

    /// This method will create a TfWarning and pass it to all delegates.
    ///
    /// If no delegates have been registered, this method will print the
    /// warning msg to stderr.
    TF_API
    void PostWarning(const TfDiagnosticBase& diagnostic);

    /// This method will create a TfStatus and pass it to all delegates.
    ///
    /// If no delegates have been registered, this method will print the
    /// status msg to stderr.
    TF_API
    void PostStatus(TfEnum statusCode, const char *statusCodeString,
        TfCallContext const &context, std::string const &commentary,
        TfDiagnosticInfo info, bool quiet);

    /// This method will create a TfStatus and pass it to all delegates.
    ///
    /// If no delegates have been registered, this method will print the
    /// status msg to stderr.
    TF_API
    void PostStatus(const TfDiagnosticBase& diagnostic);

    /// This method will issue a fatal error to all delegates.
    ///
    /// If no delegates have been registered, or if none of the delegates abort
    /// the process, this method will print the error msg and abort the process.
    [[noreturn]]
    TF_API
    void PostFatal(TfCallContext const &context, TfEnum statusCode,
                   std::string const &msg) const;

    /// Return true if an instance of TfErrorMark exists in the current thread
    /// of execution, false otherwise.
    bool HasActiveErrorMark() {
        return _markCountsAndTrapStacks.local().markCount > 0;
    }

#if !defined(doxygen)
    //
    // Public, but *only* meant to be used by the TF_ERROR() macro.
    //
    /// \private
    class ErrorHelper {
      public:
        ErrorHelper(TfCallContext const &context, TfEnum errorCode,
                    const char* errorCodeString)
            : _context(context), _errorCode(errorCode),
              _errorCodeString(errorCodeString)
        {
        }

        TF_API
        void Post(const char* fmt, ...) const
            ARCH_PRINTF_FUNCTION(2,3);

        TF_API
        void PostQuietly(const char* fmt, ...) const
            ARCH_PRINTF_FUNCTION(2,3);

        TF_API
        void Post(const std::string& msg) const;

        TF_API
        void PostWithInfo(
                const std::string& msg,
                TfDiagnosticInfo info = TfDiagnosticInfo()) const;

        TF_API
        void PostQuietly(const std::string& msg,
                         TfDiagnosticInfo info = TfDiagnosticInfo()) const;

      private:
        TfCallContext _context;
        TfEnum _errorCode;
        const char *_errorCodeString;
    };

    struct WarningHelper {
        WarningHelper(TfCallContext const &context, TfEnum warningCode,
                      const char *warningCodeString)
            : _context(context), _warningCode(warningCode),
              _warningCodeString(warningCodeString)
        {
        }

        TF_API
        void Post(const char* fmt, ...) const
            ARCH_PRINTF_FUNCTION(2,3);

        TF_API
        void PostQuietly(const char* fmt, ...) const
            ARCH_PRINTF_FUNCTION(2,3);

        TF_API
        void Post(const std::string &str) const;

        TF_API
        void PostWithInfo(
                const std::string& msg,
                TfDiagnosticInfo info = TfDiagnosticInfo()) const;

        TF_API
        void PostQuietly(const std::string& msg) const;

      private:
        TfCallContext _context;
        TfEnum _warningCode;
        const char *_warningCodeString;
    };

    struct StatusHelper {
        StatusHelper(TfCallContext const &context, TfEnum statusCode,
                     const char *statusCodeString)
            : _context(context), _statusCode(statusCode),
              _statusCodeString(statusCodeString)
        {
        }

        TF_API
        void Post(const char* fmt, ...) const
            ARCH_PRINTF_FUNCTION(2,3);

        TF_API
        void PostQuietly(const char* fmt, ...) const
            ARCH_PRINTF_FUNCTION(2,3);

        TF_API
        void Post(const std::string &str) const;

        TF_API
        void PostWithInfo(
                const std::string& msg,
                TfDiagnosticInfo info = TfDiagnosticInfo()) const;

        TF_API
        void PostQuietly(const std::string& msg) const;

      private:
        TfCallContext _context;
        TfEnum _statusCode;
        const char *_statusCodeString;
    };

    struct FatalHelper {
        FatalHelper(TfCallContext const &context, TfEnum statusCode)
            : _context(context),
              _statusCode(statusCode)
        {
        }
        [[noreturn]]
        void Post(const std::string &str) const {
            This::GetInstance().PostFatal(_context, _statusCode, str);
        }
      private:
        TfCallContext _context;
        TfEnum _statusCode;
    };
        
#endif
    
private:

    TfDiagnosticMgr();
    virtual ~TfDiagnosticMgr();
    friend class TfSingleton<This>;
    
    // Return an iterator to the first error with serial number >= mark, or the
    // past-the-end iterator, if no such errors exist.
    TF_API
    ErrorIterator _GetErrorMarkBegin(size_t mark, size_t *nErrors);

    // Invoked by ErrorMark ctor.
    inline void _CreateErrorMark() {
        ++_markCountsAndTrapStacks.local().markCount;
    }

    // Invoked by ErrorMark dtor.
    inline bool _DestroyErrorMark() {
        return --_markCountsAndTrapStacks.local().markCount == 0;
    }

    // Invoked by TfDiagnosticTrap constructor.
    void _PushTrap(TfDiagnosticTrap *trap);

    // Invoked by TfDiagnosticTrap destructor.
    void _PopTrap(TfDiagnosticTrap *trap);

    TfDiagnosticTrap *_GetActiveTrap();

    // Report an error, either via delegate or print to stderr, and issue a
    // notice if this thread of execution is the main thread.
    void _ReportError(const TfError &err);

    // Splice the errors in src into this thread's local list.  Also reassign
    // serial numbers to all the spliced errors to ensure they work correctly
    // with local error marks.
    void _SpliceErrors(ErrorList &src);

    // Helper to append pending error messages to the crash log.
    void _AppendPendingErrorsLogText(ErrorIterator i);

    // Rebuild the pending-errors crash log from startSerial onward.
    // startSerial = 0 performs a full rebuild.  The backward scan to locate the
    // dirty boundary is O(tail), not O(total).
    void _RebuildPendingErrorLogText(size_t startSerial = 0);

    // Similar log text for trapped diagnostics.
    void _AppendTrappedDiagnosticsLogText(TfDiagnosticBase const &d);

    // Rebuild the trapped-diagnostics crash log from validLogEnd onward.
    // validLogEnd = 0 performs a full rebuild.  Only entries at index >=
    // validLogEnd are re-scanned; the clean prefix is kept in place.
    void _RebuildTrappedDiagnosticsLogText(size_t validLogEnd = 0);

    // A pin held by each in-flight TfErrorTransport / TfDiagnosticTransport.
    // Prevents lazy cleanup of stale log text entries while diagnostics are
    // in transit.  handle is a shared lifetime marker -- the corresponding
    // weak_ptr in _LogTextBuffer expires when all pins are released, signalling
    // that stale entries can be cleaned up.
    struct _LogTextPin {
        std::shared_ptr<const void> handle;
    };

    // Called by TfErrorMark::Transport() to pin the pending-errors log text and
    // record the dirty boundary for a future partial rebuild.  dirtySerial is
    // the mark's _mark value (serial number threshold).
    _LogTextPin _PlaceErrorLogTextPin(size_t dirtySerial) {
        auto &logText = _pendingErrorsLogText.local();
        logText.MarkDirtyFrom(dirtySerial);
        return logText.PlacePin();
    }

    // Called by TfDiagnosticTrap::Transport() to pin the trapped-diagnostics
    // log text and record the dirty boundary for a future partial rebuild.
    // logStart is the trap's _logStart value (log text index).
    _LogTextPin _PlaceDiagnosticLogTextPin(size_t logStart) {
        auto &logText = _trappedDiagnosticsLogText.local();
        logText.MarkDirtyFrom(logStart);
        return logText.PlacePin();
    }

    // Called by TfDiagnosticTrap::_OnContentsChanged() when the trap's
    // container is mutated directly (e.g. EraseMatching, Clear*).  If the log
    // is pinned, only records the dirty boundary so the rebuild happens lazily
    // on pin expiry; otherwise rebuilds immediately from logStart onward.
    void _OnTrapContentsChanged(size_t logStart) {
        auto &logText = _trappedDiagnosticsLogText.local();
        if (logText.IsPinned()) {
            logText.MarkDirtyFrom(logStart);
        } else {
            _RebuildTrappedDiagnosticsLogText(logStart);
        }
    }

    // Helper to apply a function to all the delegates.  Return true if `fn` was
    // invoked (i.e. there were delegates to call).
    template <class Fn>
    bool _ForEachDelegate(Fn const &fn) const;

    // A guard used to protect reentrency when adding/removing
    // delegates as well as posting errors/warnings/statuses
    mutable tbb::enumerable_thread_specific<bool> _reentrantGuard;

    // The registered delegates global delegates.
    std::vector<Delegate*> _delegates;
   
    mutable TfSpinRWMutex _delegatesMutex;

    // Global serial number for sorting.
    std::atomic<size_t> _nextSerial;

    // Thread-specific error list.
    tbb::enumerable_thread_specific<ErrorList> _errorList;

    // Double-buffered vector<string> that publishes to
    // ArchSetExtraLogInfoForErrors under a fixed label.  Update calls
    // fn(active-text) *twice* in order to update the text in both buffers, and
    // it must produce the same results on each call.
    //
    // Also owns the pin state for in-flight transports: a weak_ptr that expires
    // when all outstanding pins are released, plus a flag recording whether a
    // pin has ever been placed.
    struct _LogTextBuffer {
        _LogTextBuffer() = default;
        explicit _LogTextBuffer(std::string &&label);
        template <class Fn> void Update(Fn &&fn);

        // Place a pin on this log text buffer.  Multiple transports from the
        // same source thread share a single handle; the pin expires when all
        // have been posted or dropped.
        TF_API _LogTextPin PlacePin();

        // Returns true if a pin is currently active (transports in flight).
        // Use this to skip an eager rebuild that would evict in-flight entries.
        bool IsPinned() const {
            return _pinIssued && !_pinHandle.expired();
        }

        // Returns true if a pin was previously placed and has since expired
        // (all transports resolved), and clears the pin state.  *indicator is
        // set to the dirty-from value recorded via MarkDirtyFrom() -- the
        // caller should do a partial rebuild from that boundary.
        bool WasUnpinned(size_t *indicator) {
            if (_pinIssued && _pinHandle.expired()) {
                *indicator = _validLogEndIndicator;
                _validLogEndIndicator = _LogEndMax;
                _pinIssued = false;
                _pinHandle.reset();
                return true;
            }
            return false;
        }

        // Record a dirty boundary: the log is known clean only below
        // min(current _validLogEndIndicator, indicator).  Interpretation of
        // indicator is caller-specific:
        //   - _trappedDiagnosticsLogText: a log text index (trap->_logStart)
        //   - _pendingErrorsLogText: a serial number threshold (mark._mark)
        // _LogEndMax in both cases means "no dirty boundary recorded".
        void MarkDirtyFrom(size_t indicator) {
            _validLogEndIndicator = std::min(_validLogEndIndicator, indicator);
        }

        // Return a reference to the current log text.
        const std::vector<std::string> &Get() const {
            // Both buffers always contain the same content, so just return the
            // first by convention.
            return _texts.first;
        }

    private:
        static constexpr size_t _LogEndMax = size_t(-1);
        
        std::string                   _label;
        std::pair<
            std::vector<std::string>,
            std::vector<std::string>> _texts;
        // Dirty-from indicator set by MarkDirtyFrom() at mutation time.
        // See MarkDirtyFrom() comment for per-buffer semantics.
        size_t                        _validLogEndIndicator = _LogEndMax;
        std::weak_ptr<const void>     _pinHandle;
        bool                          _pinIssued            = false;
        bool                          _parity               = false;
    };

    // Thread-specific diagnostic log text for pending errors.
    tbb::enumerable_thread_specific<_LogTextBuffer> _pendingErrorsLogText;

    // Thread-specific log text for trapped diagnostics.
    tbb::enumerable_thread_specific<_LogTextBuffer> _trappedDiagnosticsLogText;

    // Store as many trap pointers locally as we can in a cache line, minus the
    // size needed for the error mark count.
    static constexpr size_t _TrapStackLocalCap =
        TfComputeSmallVectorLocalCapacityForTotalSize<
        TfDiagnosticTrap*, ARCH_CACHE_LINE_SIZE - sizeof(size_t)>();
    
    // Struct containing thread-local error mark count, and stack of active
    // diagnostic traps, innermost last.
    struct _MarkCountAndTrapStack {
        size_t markCount = 0;
        TfSmallVector<TfDiagnosticTrap*, _TrapStackLocalCap> trapStack;
    };

    static_assert(sizeof(_MarkCountAndTrapStack) == ARCH_CACHE_LINE_SIZE);
    
    // Thread-specific error mark counts & trap stacks.  Use a native key for
    // best performance here.
    tbb::enumerable_thread_specific<
        _MarkCountAndTrapStack,
        tbb::cache_aligned_allocator<_MarkCountAndTrapStack>,
        tbb::ets_key_per_instance> _markCountsAndTrapStacks;

    bool _quiet;

    friend class Tf_DiagnosticContainer;
    friend class Tf_DiagnosticMgrTestAccess;
    friend class TfDiagnosticTransport;
    friend class TfDiagnosticTrap;
    friend class TfError;
    friend class TfErrorTransport;
    friend class TfErrorMark;
};

TF_API_TEMPLATE_CLASS(TfSingleton<TfDiagnosticMgr>);

PXR_NAMESPACE_CLOSE_SCOPE

#endif // PXR_BASE_TF_DIAGNOSTIC_MGR_H
