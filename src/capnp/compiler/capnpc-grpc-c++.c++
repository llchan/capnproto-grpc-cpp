#include <capnp/schema-loader.h>
#include <capnp/serialize.h>

#include <kj/main.h>
#include <kj/miniposix.h>

#include <fcntl.h>
#include <sys/stat.h>

#ifndef VERSION
#define VERSION "(unknown)"
#endif

namespace capnp {
namespace grpc {
namespace {

class CapnpcGrpcCppMain {
public:
  CapnpcGrpcCppMain(kj::ProcessContext& context): context(context) {}

  kj::MainFunc getMain() {
    return kj::MainBuilder(context, "Cap'n Proto gRPC C++ plugin version " VERSION,
          "This is a Cap'n Proto compiler plugin which generates gRPC C++ code. "
          "It is meant to be run using the Cap'n Proto compiler, e.g.:\n"
          "    capnp compile -ogrpc-c++ foo.capnp")
        .callAfterParsing(KJ_BIND_METHOD(*this, run))
        .build();
  }

private:
  kj::ProcessContext& context;
  SchemaLoader schemaLoader;

  struct FileText {
    kj::StringTree header;
    kj::StringTree source;
  };

  FileText makeFileText(Schema schema,
                        schema::CodeGeneratorRequest::RequestedFile::Reader request) {
    return FileText {
    };
  }

  void makeDirectory(kj::StringPtr path) {
    KJ_IF_MAYBE(slashpos, path.findLast('/')) {
      // Make the parent dir.
      makeDirectory(kj::str(path.slice(0, *slashpos)));
    }

    if (kj::miniposix::mkdir(path.cStr(), 0777) < 0) {
      int error = errno;
      if (error != EEXIST) {
        KJ_FAIL_SYSCALL("mkdir(path)", error, path);
      }
    }
  }

  void writeFile(kj::StringPtr filename, const kj::StringTree& text) {
    if (!filename.startsWith("/")) {
      KJ_IF_MAYBE(slashpos, filename.findLast('/')) {
        // Make the parent dir.
        makeDirectory(kj::str(filename.slice(0, *slashpos)));
      }
    }

    int fd;
    KJ_SYSCALL(fd = open(filename.cStr(), O_CREAT | O_WRONLY | O_TRUNC, 0666), filename);
    kj::FdOutputStream out((kj::AutoCloseFd(fd)));

    text.visit(
        [&](kj::ArrayPtr<const char> text) {
          out.write(text.begin(), text.size());
        });
  }

  kj::MainBuilder::Validity run() {
    ReaderOptions options;
    options.traversalLimitInWords = 1 << 30;  // Don't limit.
    StreamFdMessageReader reader(STDIN_FILENO, options);
    auto request = reader.getRoot<schema::CodeGeneratorRequest>();

    auto capnpVersion = request.getCapnpVersion();

    if (capnpVersion.getMajor() != CAPNP_VERSION_MAJOR ||
        capnpVersion.getMinor() != CAPNP_VERSION_MINOR ||
        capnpVersion.getMicro() != CAPNP_VERSION_MICRO) {
      auto compilerVersion = request.hasCapnpVersion()
          ? kj::str(capnpVersion.getMajor(), '.', capnpVersion.getMinor(), '.',
                    capnpVersion.getMicro())
          : kj::str("pre-0.6");  // pre-0.6 didn't send the version.
      auto generatorVersion = kj::str(
          CAPNP_VERSION_MAJOR, '.', CAPNP_VERSION_MINOR, '.', CAPNP_VERSION_MICRO);

      KJ_LOG(WARNING,
          "You appear to be using different versions of 'capnp' (the compiler) and "
          "'capnpc-grpc-c++' (the code generator). This can happen, for example, if you built "
          "a custom version of 'capnp' but then ran it with '-ogrpc-c++', which invokes "
          "'capnpc-grpc-c++' from your PATH (i.e. the installed version). To specify an alternate "
          "'capnpc-grpc-c++' executable, try something like '-o/path/to/capnpc-grpc-c++' instead.",
          compilerVersion, generatorVersion);
    }

    for (auto node: request.getNodes()) {
      schemaLoader.load(node);
    }

    kj::FdOutputStream rawOut(STDOUT_FILENO);
    kj::BufferedOutputStreamWrapper out(rawOut);

    for (auto requestedFile: request.getRequestedFiles()) {
      auto schema = schemaLoader.get(requestedFile.getId());
      auto fileText = makeFileText(schema, requestedFile);

      writeFile(kj::str(schema.getProto().getDisplayName(), ".h"), fileText.header);
      writeFile(kj::str(schema.getProto().getDisplayName(), ".c++"), fileText.source);
    }

    return true;
  }
};

}  // namespace
}  // namespace grpc
}  // namespace capnp

KJ_MAIN(capnp::grpc::CapnpcGrpcCppMain);
