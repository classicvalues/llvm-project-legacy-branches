//===--- SourceManager.h - Track and cache source files ---------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines the SourceManager interface.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_SOURCEMANAGER_H
#define LLVM_CLANG_SOURCEMANAGER_H

#include "clang/Basic/SourceLocation.h"
#include "llvm/Bitcode/SerializationFwd.h"
#include <vector>
#include <set>
#include <list>
#include <cassert>

namespace llvm {
class MemoryBuffer;
}
  
namespace clang {
  
class SourceManager;
class FileManager;
class FileEntry;
class IdentifierTokenInfo;

/// SrcMgr - Public enums and private classes that are part of the
/// SourceManager implementation.
///
namespace SrcMgr {
  /// CharacteristicKind - This is used to represent whether a file or directory
  /// holds normal user code, system code, or system code which is implicitly
  /// 'extern "C"' in C++ mode.  Entire directories can be tagged with this
  /// (this is maintained by DirectoryLookup and friends) as can specific
  /// FileIDInfos when a #pragma system_header is seen or various other cases.
  ///
  enum CharacteristicKind {
    C_User, C_System, C_ExternCSystem
  };
  
  /// ContentCache - Once instance of this struct is kept for every file
  /// loaded or used.  This object owns the MemoryBuffer object.
  class ContentCache {
    /// Buffer - The actual buffer containing the characters from the input
    /// file.  This is owned by the ContentCache object.
    mutable const llvm::MemoryBuffer *Buffer;

  public:
    /// Reference to the file entry.  This reference does not own
    /// the FileEntry object.  It is possible for this to be NULL if
    /// the ContentCache encapsulates an imaginary text buffer.
    const FileEntry *Entry;
    
    /// SourceLineCache - A new[]'d array of offsets for each source line.  This
    /// is lazily computed.  This is owned by the ContentCache object.
    unsigned *SourceLineCache;
    
    /// NumLines - The number of lines in this ContentCache.  This is only valid
    /// if SourceLineCache is non-null.
    unsigned NumLines;
    
    /// getBuffer - Returns the memory buffer for the associated content.
    const llvm::MemoryBuffer *getBuffer() const;
    
    /// getSize - Returns the size of the content encapsulated by this
    ///  ContentCache. This can be the size of the source file or the size of an
    ///  arbitrary scratch buffer.  If the ContentCache encapsulates a source
    ///  file this size is retrieved from the file's FileEntry.
    unsigned getSize() const;
    
    /// getSizeBytesMapped - Returns the number of bytes actually mapped for
    ///  this ContentCache.  This can be 0 if the MemBuffer was not actually
    ///  instantiated.
    unsigned getSizeBytesMapped() const;
    
    void setBuffer(const llvm::MemoryBuffer *B) {
      assert(!Buffer && "MemoryBuffer already set.");
      Buffer = B;
    }
        
    ContentCache(const FileEntry *e = NULL)
      : Buffer(NULL), Entry(e), SourceLineCache(NULL), NumLines(0) {}

    ~ContentCache();
    
    /// The copy ctor does not allow copies where source object has either
    ///  a non-NULL Buffer or SourceLineCache.  Ownership of allocated memory
    ///  is not transfered, so this is a logical error.
    ContentCache(const ContentCache &RHS) : Buffer(NULL),SourceLineCache(NULL) {
      Entry = RHS.Entry;

      assert (RHS.Buffer == NULL && RHS.SourceLineCache == NULL
              && "Passed ContentCache object cannot own a buffer.");
              
      NumLines = RHS.NumLines;      
    }
    
    /// Emit - Emit this ContentCache to Bitcode.
    void Emit(llvm::Serializer &S) const;
    
    /// ReadToSourceManager - Reconstitute a ContentCache from Bitcode
    //   and store it in the specified SourceManager.
    static void ReadToSourceManager(llvm::Deserializer &D, SourceManager &SM,
                                    FileManager *FMgr, std::vector<char> &Buf);
    
  private:
    // Disable assignments.
    ContentCache &operator=(const ContentCache& RHS);    
  };  

  /// FileIDInfo - Information about a FileID, basically just the logical file
  /// that it represents and include stack information.  A File SourceLocation
  /// is a byte offset from the start of this.
  ///
  /// FileID's are used to compute the location of a character in memory as well
  /// as the instantiation source location, which can be differ from the
  /// spelling location.  It is different when #line's are active or when macros
  /// have been expanded.
  ///
  /// Each FileID has include stack information, indicating where it came from.
  /// For the primary translation unit, it comes from SourceLocation() aka 0.
  /// This information encodes the #include chain that a token was instantiated
  /// from.
  ///
  /// FileIDInfos contain a "ContentCache *", describing the source file, 
  /// and a Chunk number, which allows a SourceLocation to index into very
  /// large files (those which there are not enough FilePosBits to address).
  ///
  struct FileIDInfo {
  private:
    /// IncludeLoc - The location of the #include that brought in this file.
    /// This SourceLocation object has an invalid SLOC for the main file.
    SourceLocation IncludeLoc;
    
    /// ChunkNo - Really large buffers are broken up into chunks that are
    /// each (1 << SourceLocation::FilePosBits) in size.  This specifies the
    /// chunk number of this FileID.
    unsigned ChunkNo : 30;
    
    /// FileCharacteristic - This is an instance of CharacteristicKind,
    /// indicating whether this is a system header dir or not.
    unsigned FileCharacteristic : 2;
    
    /// Content - Information about the source buffer itself.
    const ContentCache *Content;

  public:
    /// get - Return a FileIDInfo object.
    static FileIDInfo get(SourceLocation IL, unsigned CN, 
                          const ContentCache *Con,
                          CharacteristicKind FileCharacter) {
      FileIDInfo X;
      X.IncludeLoc = IL;
      X.ChunkNo = CN;
      X.Content = Con;
      X.FileCharacteristic = FileCharacter;
      return X;
    }
    
    SourceLocation getIncludeLoc() const { return IncludeLoc; }
    unsigned getChunkNo() const { return ChunkNo; }
    const ContentCache* getContentCache() const { return Content; }

    /// getCharacteristic - Return whether this is a system header or not.
    CharacteristicKind getFileCharacteristic() const { 
      return (CharacteristicKind)FileCharacteristic;
    }
    
    /// Emit - Emit this FileIDInfo to Bitcode.
    void Emit(llvm::Serializer& S) const;
    
    /// ReadVal - Reconstitute a FileIDInfo from Bitcode.
    static FileIDInfo ReadVal(llvm::Deserializer& S);
  };
  
  /// MacroIDInfo - Macro SourceLocations refer to these records by their ID.
  /// Each MacroIDInfo encodes the Instantiation location - where the macro was
  /// instantiated, and the SpellingLoc - where the actual character data for
  /// the token came from.  An actual macro SourceLocation stores deltas from
  /// these positions.
  class MacroIDInfo {
    SourceLocation InstantiationLoc, SpellingLoc;
  public:
    SourceLocation getInstantiationLoc() const { return InstantiationLoc; }
    SourceLocation getSpellingLoc() const { return SpellingLoc; }
    
    /// get - Return a MacroID for a macro expansion.  VL specifies
    /// the instantiation location (where the macro is expanded), and SL
    /// specifies the spelling location (where the characters from the token
    /// come from).  Both VL and PL refer to normal File SLocs.
    static MacroIDInfo get(SourceLocation VL, SourceLocation SL) {
      MacroIDInfo X;
      X.InstantiationLoc = VL;
      X.SpellingLoc = SL;
      return X;
    }
    
    /// Emit - Emit this MacroIDInfo to Bitcode.
    void Emit(llvm::Serializer& S) const;
    
    /// ReadVal - Reconstitute a MacroIDInfo from Bitcode.
    static MacroIDInfo ReadVal(llvm::Deserializer& S);
  };
}  // end SrcMgr namespace.
} // end clang namespace

namespace std {
template <> struct less<clang::SrcMgr::ContentCache> {
  inline bool operator()(const clang::SrcMgr::ContentCache& L,
                         const clang::SrcMgr::ContentCache& R) const {
    return L.Entry < R.Entry;
  }
};
} // end std namespace

namespace clang {
  
/// SourceManager - This file handles loading and caching of source files into
/// memory.  This object owns the MemoryBuffer objects for all of the loaded
/// files and assigns unique FileID's for each unique #include chain.
///
/// The SourceManager can be queried for information about SourceLocation
/// objects, turning them into either spelling or instantiation locations.
/// Spelling locations represent where the bytes corresponding to a token came
/// from and instantiation locations represent where the location is in the
/// user's view.  In the case of a macro expansion, for example, the spelling
/// location indicates where the expanded token came from and the instantiation
/// location specifies where it was expanded.
class SourceManager {
  /// FileInfos - Memoized information about all of the files tracked by this
  /// SourceManager.  This set allows us to merge ContentCache entries based
  /// on their FileEntry*.  All ContentCache objects will thus have unique,
  /// non-null, FileEntry pointers.  
  std::set<SrcMgr::ContentCache> FileInfos;
  
  /// MemBufferInfos - Information about various memory buffers that we have
  /// read in.  This is a list, instead of a vector, because we need pointers to
  /// the ContentCache objects to be stable.  All FileEntry* within the
  /// stored ContentCache objects are NULL, as they do not refer to a file.
  std::list<SrcMgr::ContentCache> MemBufferInfos;
  
  /// FileIDs - Information about each FileID.  FileID #0 is not valid, so all
  /// entries are off by one.
  std::vector<SrcMgr::FileIDInfo> FileIDs;
  
  /// MacroIDs - Information about each MacroID.
  std::vector<SrcMgr::MacroIDInfo> MacroIDs;
  
  /// LastLineNo - These ivars serve as a cache used in the getLineNumber
  /// method which is used to speedup getLineNumber calls to nearby locations.
  mutable FileID LastLineNoFileIDQuery;
  mutable SrcMgr::ContentCache *LastLineNoContentCache;
  mutable unsigned LastLineNoFilePos;
  mutable unsigned LastLineNoResult;
  
  /// MainFileID - The file ID for the main source file of the translation unit.
  FileID MainFileID;

  // SourceManager doesn't support copy construction.
  explicit SourceManager(const SourceManager&);
  void operator=(const SourceManager&);  
public:
  SourceManager() {}
  ~SourceManager() {}
  
  void clearIDTables() {
    MainFileID = FileID();
    FileIDs.clear();
    MacroIDs.clear();
    LastLineNoFileIDQuery = FileID();
    LastLineNoContentCache = 0;
  }

  //===--------------------------------------------------------------------===//
  // MainFileID creation and querying methods.
  //===--------------------------------------------------------------------===//

  /// getMainFileID - Returns the FileID of the main source file.
  FileID getMainFileID() const { return MainFileID; }
  
  /// createMainFileID - Create the FileID for the main source file.
  FileID createMainFileID(const FileEntry *SourceFile,
                          SourceLocation IncludePos) {
    assert(MainFileID.isInvalid() && "MainFileID already set!");
    MainFileID = createFileID(SourceFile, IncludePos, SrcMgr::C_User);
    return MainFileID;
  }
  
  //===--------------------------------------------------------------------===//
  // Methods to create new FileID's.
  //===--------------------------------------------------------------------===//
  
  /// createFileID - Create a new FileID that represents the specified file
  /// being #included from the specified IncludePosition.  This returns 0 on
  /// error and translates NULL into standard input.
  FileID createFileID(const FileEntry *SourceFile, SourceLocation IncludePos,
                      SrcMgr::CharacteristicKind FileCharacter) {
    const SrcMgr::ContentCache *IR = getContentCache(SourceFile);
    if (IR == 0) return FileID();    // Error opening file?
    return createFileID(IR, IncludePos, FileCharacter);
  }
  
  /// createFileIDForMemBuffer - Create a new FileID that represents the
  /// specified memory buffer.  This does no caching of the buffer and takes
  /// ownership of the MemoryBuffer, so only pass a MemoryBuffer to this once.
  FileID createFileIDForMemBuffer(const llvm::MemoryBuffer *Buffer) {
    return createFileID(createMemBufferContentCache(Buffer), SourceLocation(),
                        SrcMgr::C_User);
  }
  
  /// createMainFileIDForMembuffer - Create the FileID for a memory buffer
  ///  that will represent the FileID for the main source.  One example
  ///  of when this would be used is when the main source is read from STDIN.
  FileID createMainFileIDForMemBuffer(const llvm::MemoryBuffer *Buffer) {
    assert(MainFileID.isInvalid() && "MainFileID already set!");
    MainFileID = createFileIDForMemBuffer(Buffer);
    return MainFileID;
  }

  //===--------------------------------------------------------------------===//
  // FileID manipulation methods.
  //===--------------------------------------------------------------------===//
  
  /// getBuffer - Return the buffer for the specified FileID.
  ///
  const llvm::MemoryBuffer *getBuffer(FileID FID) const {
    return getContentCache(FID)->getBuffer();
  }
  
  /// getFileEntryForID - Returns the FileEntry record for the provided FileID.
  const FileEntry *getFileEntryForID(FileID FID) const {
    return getContentCache(FID)->Entry;
  }
  
  /// getBufferData - Return a pointer to the start and end of the source buffer
  /// data for the specified FileID.
  std::pair<const char*, const char*> getBufferData(FileID FID) const;
  
  
  //===--------------------------------------------------------------------===//
  // SourceLocation manipulation methods.
  //===--------------------------------------------------------------------===//
  
  /// getLocForStartOfFile - Return the source location corresponding to the
  /// first byte of the specified file.
  SourceLocation getLocForStartOfFile(FileID FID) const {
    return SourceLocation::getFileLoc(FID.ID, 0);
  }
  
  /// getInstantiationLoc - Return a new SourceLocation that encodes the fact
  /// that a token at Loc should actually be referenced from InstantiationLoc.
  SourceLocation getInstantiationLoc(SourceLocation Loc,
                                     SourceLocation InstantiationLoc);
  
   /// getIncludeLoc - Return the location of the #include for the specified
  /// SourceLocation.  If this is a macro expansion, this transparently figures
  /// out which file includes the file being expanded into.
  SourceLocation getIncludeLoc(SourceLocation ID) const {
    return getFIDInfo(getInstantiationLoc(ID).getChunkID())->getIncludeLoc();
  }
  
  /// getCharacterData - Return a pointer to the start of the specified location
  /// in the appropriate MemoryBuffer.
  const char *getCharacterData(SourceLocation SL) const;
  
  /// getColumnNumber - Return the column # for the specified file position.
  /// This is significantly cheaper to compute than the line number.  This
  /// returns zero if the column number isn't known.  This may only be called on
  /// a file sloc, so you must choose a spelling or instantiation location
  /// before calling this method.
  unsigned getColumnNumber(SourceLocation Loc) const;
  
  unsigned getSpellingColumnNumber(SourceLocation Loc) const {
    return getColumnNumber(getSpellingLoc(Loc));
  }
  unsigned getInstantiationColumnNumber(SourceLocation Loc) const {
    return getColumnNumber(getInstantiationLoc(Loc));
  }
  
  
  /// getLineNumber - Given a SourceLocation, return the spelling line number
  /// for the position indicated.  This requires building and caching a table of
  /// line offsets for the MemoryBuffer, so this is not cheap: use only when
  /// about to emit a diagnostic.
  unsigned getLineNumber(SourceLocation Loc) const;

  unsigned getInstantiationLineNumber(SourceLocation Loc) const {
    return getLineNumber(getInstantiationLoc(Loc));
  }
  unsigned getSpellingLineNumber(SourceLocation Loc) const {
    return getLineNumber(getSpellingLoc(Loc));
  }
  
  /// getSourceName - This method returns the name of the file or buffer that
  /// the SourceLocation specifies.  This can be modified with #line directives,
  /// etc.
  const char *getSourceName(SourceLocation Loc) const;

  /// Given a SourceLocation object, return the instantiation location
  /// referenced by the ID.
  SourceLocation getInstantiationLoc(SourceLocation Loc) const {
    // File locations work.
    if (Loc.isFileID()) return Loc;
    
    return MacroIDs[Loc.getMacroID()].getInstantiationLoc();
  }
  
  /// getSpellingLoc - Given a SourceLocation object, return the spelling
  /// location referenced by the ID.  This is the place where the characters
  /// that make up the lexed token can be found.
  SourceLocation getSpellingLoc(SourceLocation Loc) const {
    // File locations work!
    if (Loc.isFileID()) return Loc;
    
    // Look up the macro token's spelling location.
    SourceLocation PLoc = MacroIDs[Loc.getMacroID()].getSpellingLoc();
    return PLoc.getFileLocWithOffset(Loc.getMacroSpellingOffs());
  }

  /// getDecomposedFileLoc - Decompose the specified file location into a raw
  /// FileID + Offset pair.  The first element is the FileID, the second is the
  /// offset from the start of the buffer of the location.
  std::pair<FileID, unsigned> getDecomposedFileLoc(SourceLocation Loc) const {
    assert(Loc.isFileID() && "Isn't a File SourceLocation");
    
    // TODO: Add a flag "is first chunk" to SLOC.
    const SrcMgr::FileIDInfo *FIDInfo = getFIDInfo(Loc.getChunkID());
      
    // If this file has been split up into chunks, factor in the chunk number
    // that the FileID references.
    unsigned ChunkNo = FIDInfo->getChunkNo();
    unsigned Offset = Loc.getRawFilePos();
    Offset += (ChunkNo << SourceLocation::FilePosBits);

    assert(Loc.getChunkID() >= ChunkNo && "Unexpected offset");
    
    return std::make_pair(FileID::Create(Loc.getChunkID()-ChunkNo), Offset);
  }
  
  /// getFileID - Return the FileID for a SourceLocation.
  ///
  FileID getFileID(SourceLocation SpellingLoc) const {
    return getDecomposedFileLoc(SpellingLoc).first;
  }    
  
  /// getFullFilePos - This (efficient) method returns the offset from the start
  /// of the file that the specified spelling SourceLocation represents.  This
  /// returns the location of the actual character data, not the instantiation
  /// position.
  unsigned getFullFilePos(SourceLocation SpellingLoc) const {
    return getDecomposedFileLoc(SpellingLoc).second;
  }
  
  /// isFromSameFile - Returns true if both SourceLocations correspond to
  ///  the same file.
  bool isFromSameFile(SourceLocation Loc1, SourceLocation Loc2) const {
    return getFileID(Loc1) == getFileID(Loc2);
  }
  
  /// isFromMainFile - Returns true if the file of provided SourceLocation is
  ///   the main file.
  bool isFromMainFile(SourceLocation Loc) const {
    return getFileID(Loc) == getMainFileID();
  } 

  /// isInSystemHeader - Returns if a SourceLocation is in a system header.
  bool isInSystemHeader(SourceLocation Loc) const {
    return getFileCharacteristic(Loc) != SrcMgr::C_User;
  }
  SrcMgr::CharacteristicKind getFileCharacteristic(SourceLocation Loc) const {
    return getFIDInfo(getSpellingLoc(Loc).getChunkID())
                  ->getFileCharacteristic();
  }
  
  //===--------------------------------------------------------------------===//
  // Other miscellaneous methods.
  //===--------------------------------------------------------------------===//
  
  // Iterators over FileInfos.
  typedef std::set<SrcMgr::ContentCache>::const_iterator fileinfo_iterator;
  fileinfo_iterator fileinfo_begin() const { return FileInfos.begin(); }
  fileinfo_iterator fileinfo_end() const { return FileInfos.end(); }

  /// PrintStats - Print statistics to stderr.
  ///
  void PrintStats() const;

  /// Emit - Emit this SourceManager to Bitcode.
  void Emit(llvm::Serializer& S) const;
  
  /// Read - Reconstitute a SourceManager from Bitcode.
  static SourceManager* CreateAndRegister(llvm::Deserializer& S,
                                          FileManager &FMgr);
  
private:
  friend struct SrcMgr::ContentCache; // Used for deserialization.
  
  /// createFileID - Create a new fileID for the specified ContentCache and
  ///  include position.  This works regardless of whether the ContentCache
  ///  corresponds to a file or some other input source.
  FileID createFileID(const SrcMgr::ContentCache* File,
                      SourceLocation IncludePos,
                      SrcMgr::CharacteristicKind DirCharacter);
    
  /// getContentCache - Create or return a cached ContentCache for the specified
  ///  file.  This returns null on failure.
  const SrcMgr::ContentCache* getContentCache(const FileEntry *SourceFile);

  /// createMemBufferContentCache - Create a new ContentCache for the specified
  ///  memory buffer.
  const SrcMgr::ContentCache* 
  createMemBufferContentCache(const llvm::MemoryBuffer *Buf);

  const SrcMgr::FileIDInfo *getFIDInfo(unsigned FID) const {
    assert(FID-1 < FileIDs.size() && "Invalid FileID!");
    return &FileIDs[FID-1];
  }
  const SrcMgr::FileIDInfo *getFIDInfo(FileID FID) const {
    return getFIDInfo(FID.ID);
  }
  
  const SrcMgr::ContentCache *getContentCache(FileID FID) const {
    return getContentCache(getFIDInfo(FID.ID));
  }
  
  /// Return the ContentCache structure for the specified FileID.  
  ///  This is always the physical reference for the ID.
  const SrcMgr::ContentCache*
  getContentCache(const SrcMgr::FileIDInfo* FIDInfo) const {
    return FIDInfo->getContentCache();
  }  
};


}  // end namespace clang

#endif
