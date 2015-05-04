#pragma once

#include <crossbow/infinio/InfinibandBuffer.hpp>

#include <boost/system/error_code.hpp>

#include <cstdint>

#include <rdma/rdma_cma.h>

namespace crossbow {
namespace infinio {

class Endpoint;
class InfinibandService;
class InfinibandBaseHandler;
class SocketImplementation;

/**
 * @brief Base class containing common functionality shared between InfinibandAcceptor and InfinibandSocket
 */
class InfinibandBaseSocket {
public:
    void open(boost::system::error_code& ec);

    bool isOpen() const {
        return (mImpl != nullptr);
    }

    void close(boost::system::error_code& ec);

    void bind(const Endpoint& addr, boost::system::error_code& ec);

    void setHandler(InfinibandBaseHandler* handler);

protected:
    InfinibandBaseSocket(InfinibandService& transport)
            : mTransport(transport),
              mImpl(nullptr) {
    }

    InfinibandBaseSocket(InfinibandService& transport, SocketImplementation* impl)
            : mTransport(transport),
              mImpl(impl) {
    }

    // TODO Owner of the SocketImplementation pointer is unclear (i.e. never gets deleted)

    SocketImplementation* mImpl;

    InfinibandService& mTransport;
};

/**
 * @brief Socket acceptor to listen for new incoming connections
 */
class InfinibandAcceptor: public InfinibandBaseSocket {
public:
    InfinibandAcceptor(InfinibandService& transport)
            : InfinibandBaseSocket(transport) {
    }

    void listen(int backlog, boost::system::error_code& ec);
};

/**
 * @brief Socket class to communicate with a remote host
 */
class InfinibandSocket: public InfinibandBaseSocket {
public:
    InfinibandSocket(InfinibandService& transport)
            : InfinibandBaseSocket(transport) {
    }

    InfinibandSocket(InfinibandService& transport, SocketImplementation* impl)
            : InfinibandBaseSocket(transport, impl) {
    }

    void connect(const Endpoint& addr, boost::system::error_code& ec);

    void disconnect(boost::system::error_code& ec);

    void send(InfinibandBuffer& buffer, boost::system::error_code& ec);

    InfinibandBuffer acquireBuffer(size_t length);

    void releaseBuffer(uint32_t id);
};

/**
 * @brief Interface class containing callbacks for various socket events
 */
class InfinibandBaseHandler {
public:
    virtual ~InfinibandBaseHandler();

    /**
     * @brief Handle a new incoming connection
     *
     * The connection is not yet in a fully connected state so any write operations on the socket will fail until the
     * onConnected callback is invoked from the socket.
     *
     * In case the connection is accepted the class is responsible for taking ownership of the InfinibandSocket object.
     *
     * @param socket Socket to the remote host
     *
     * @return True if the connection was accepted, false if it was rejected
     */
    virtual bool onConnection(InfinibandSocket socket) = 0;

    /**
     * @brief Invoked when the connection to the remote host was established
     *
     * The socket is now able to send and receive messages to and from the remote host.
     *
     * Beware of race conditions in a multithreaded execution environment: The remote end might start sending data
     * before the onConnected function is executed, in this case another thread might call onReceive before onConnected.
     *
     * @param ec Error in case the connection attempt failed
     */
    virtual void onConnected(const boost::system::error_code& ec) = 0;

    /**
     * @brief Invoked whenever data was received from the remote host
     *
     * The function must not release the buffer containing the data.
     *
     * @param buffer Pointer to the transmitted data
     * @param length Number of bytes transmitted
     * @param ec Error in case the receive failed
     */
    virtual void onReceive(const InfinibandBuffer& buffer, size_t length, const boost::system::error_code& ec) = 0;

    /**
     * @brief Invoked whenever data was sent to the remote host
     *
     * The function must not release the buffer containing the data.
     *
     * @param buffer Pointer to the transmitted data
     * @param length Number of bytes transmitted
     * @param ec Error in case the send failed
     */
    virtual void onSend(const InfinibandBuffer& buffer, size_t length, const boost::system::error_code& ec) = 0;

    /**
     * @brief Invoked whenever the remote host disconnected
     *
     * In order to shutdown the connection the handler should also disconnect from the remote host.
     *
     * Receives may be triggered even after this callback was invoked from remaining packets that were in flight.
     */
    virtual void onDisconnect() = 0;

    /**
     * @brief Invoked whenever the connection is disconnected
     *
     * Any remaining in flight packets were processed, it is now safe to clean up the connection.
     */
    virtual void onDisconnected() = 0;
};

/**
 * @brief Handler interface to handle events of an InfinibandSocket
 *
 * Hides any methods that are not applicable on this type of socket.
 */
class InfinibandSocketHandler: public InfinibandBaseHandler {
private:
    virtual bool onConnection(InfinibandSocket socket) final override;
};

/**
 * @brief Handler interface to handle events of an InfinibandAcceptor
 *
 * Hides any methods that are not applicable on this type of socket.
 */
class InfinibandAcceptorHandler: public InfinibandBaseHandler {
private:
    virtual void onConnected(const boost::system::error_code& ec) final override;

    virtual void onReceive(const InfinibandBuffer& buffer, size_t length, const boost::system::error_code& ec) final
        override;

    virtual void onSend(const InfinibandBuffer& buffer, size_t length, const boost::system::error_code& ec) final
        override;

    virtual void onDisconnect() final override;

    virtual void onDisconnected() final override;
};

} // namespace infinio
} // namespace crossbow
