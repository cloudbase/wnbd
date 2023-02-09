#include "ksocket.h"

//////////////////////////////////////////////////////////////////////////
// Definitions.
//////////////////////////////////////////////////////////////////////////

#define MEMORY_TAG            '  sK'

//////////////////////////////////////////////////////////////////////////
// Structures.
//////////////////////////////////////////////////////////////////////////

typedef struct _KSOCKET_ASYNC_CONTEXT
{
  KEVENT CompletionEvent;
  PIRP Irp;
} KSOCKET_ASYNC_CONTEXT, *PKSOCKET_ASYNC_CONTEXT;

typedef struct _KSOCKET
{
  PWSK_SOCKET	WskSocket;

  union
  {
    PVOID WskDispatch;

    PWSK_PROVIDER_CONNECTION_DISPATCH WskConnectionDispatch;
    PWSK_PROVIDER_LISTEN_DISPATCH WskListenDispatch;
    PWSK_PROVIDER_DATAGRAM_DISPATCH WskDatagramDispatch;
#if (NTDDI_VERSION >= NTDDI_WIN10_RS2)
    PWSK_PROVIDER_STREAM_DISPATCH WskStreamDispatch;
#endif
  };
  LONG operation;
  KSOCKET_ASYNC_CONTEXT AsyncContextRead;
  KSOCKET_ASYNC_CONTEXT AsyncContextWrite;
} KSOCKET, *PKSOCKET;

//////////////////////////////////////////////////////////////////////////
// Variables.
//////////////////////////////////////////////////////////////////////////

WSK_REGISTRATION     WskRegistration = { 0 };
WSK_PROVIDER_NPI     WskProvider = { 0 };
WSK_CLIENT_DISPATCH  WskDispatch = { MAKE_WSK_VERSION(1,0), 0, NULL };

BOOLEAN WskInitialized = FALSE;

//////////////////////////////////////////////////////////////////////////
// Function prototypes.
//////////////////////////////////////////////////////////////////////////

NTSTATUS
NTAPI
KspAsyncContextAllocate(
  _Out_ PKSOCKET_ASYNC_CONTEXT AsyncContext
  );

VOID
NTAPI
KspAsyncContextFree(
  _In_ PKSOCKET_ASYNC_CONTEXT AsyncContext
  );

VOID
NTAPI
KspAsyncContextReset(
  _In_ PKSOCKET_ASYNC_CONTEXT AsyncContext
  );

NTSTATUS
NTAPI
KspAsyncContextCompletionRoutine(
  _In_ PDEVICE_OBJECT	DeviceObject,
  _In_ PIRP Irp,
  _In_ PKEVENT CompletionEvent
  );

NTSTATUS
NTAPI
KspAsyncContextWaitForCompletion(
  _In_ PKSOCKET_ASYNC_CONTEXT AsyncContext,
  _Inout_ PNTSTATUS Status
  );

//////////////////////////////////////////////////////////////////////////
// Private functions.
//////////////////////////////////////////////////////////////////////////

NTSTATUS
NTAPI
KspAsyncContextAllocate(
  _Out_ PKSOCKET_ASYNC_CONTEXT AsyncContext
  )
{
  //
  // Initialize the completion event.
  //

  KeInitializeEvent(
    &AsyncContext->CompletionEvent,
    SynchronizationEvent,
    FALSE
    );

  //
  // Initialize the IRP.
  //

  AsyncContext->Irp = IoAllocateIrp(1, FALSE);

  if (AsyncContext->Irp == NULL)
  {
    return STATUS_INSUFFICIENT_RESOURCES;
  }

  //
  // KspAsyncContextCompletionRoutine will set
  // the CompletionEvent.
  //

  IoSetCompletionRoutine(
    AsyncContext->Irp,
    &KspAsyncContextCompletionRoutine,
    &AsyncContext->CompletionEvent,
    TRUE,
    TRUE,
    TRUE
    );

  return STATUS_SUCCESS;
}

VOID
NTAPI
KspAsyncContextFree(
  _In_ PKSOCKET_ASYNC_CONTEXT AsyncContext
  )
{
  //
  // Free the IRP.
  //

  IoFreeIrp(AsyncContext->Irp);
}

VOID
NTAPI
KspAsyncContextReset(
  _In_ PKSOCKET_ASYNC_CONTEXT AsyncContext
  )
{
  //
  // If the WSK application allocated the IRP, or is reusing an IRP
  // that it previously allocated, then it must set an IoCompletion
  // routine for the IRP before calling a WSK function.  In this
  // situation, the WSK application must specify TRUE for the
  // InvokeOnSuccess, InvokeOnError, and InvokeOnCancel parameters that
  // are passed to the IoSetCompletionRoutine function to ensure that
  // the IoCompletion routine is always called. Furthermore, the IoCompletion
  // routine that is set for the IRP must always return
  // STATUS_MORE_PROCESSING_REQUIRED to terminate the completion processing
  // of the IRP.  If the WSK application is done using the IRP after the
  // IoCompletion routine has been called, then it should call the IoFreeIrp
  // function to free the IRP before returning from the IoCompletion routine.
  // If the WSK application does not free the IRP then it can reuse the IRP
  // for a call to another WSK function.
  //
  // (ref: https://docs.microsoft.com/en-us/windows-hardware/drivers/network/using-irps-with-winsock-kernel-functions)
  //

  //
  // Reset the completion event.
  //

  KeResetEvent(&AsyncContext->CompletionEvent);

  //
  // Reuse the IRP.
  //

  IoReuseIrp(AsyncContext->Irp, STATUS_UNSUCCESSFUL);

  IoSetCompletionRoutine(
    AsyncContext->Irp,
    &KspAsyncContextCompletionRoutine,
    &AsyncContext->CompletionEvent,
    TRUE,
    TRUE,
    TRUE
    );
}

NTSTATUS
NTAPI
KspAsyncContextCompletionRoutine(
  _In_ PDEVICE_OBJECT	DeviceObject,
  _In_ PIRP Irp,
  _In_ PKEVENT CompletionEvent
  )
{
  UNREFERENCED_PARAMETER(DeviceObject);
  UNREFERENCED_PARAMETER(Irp);

  KeSetEvent(CompletionEvent, IO_NO_INCREMENT, FALSE);
  return STATUS_MORE_PROCESSING_REQUIRED;
}

NTSTATUS
NTAPI
KspNoWaitCompletionRoutine(
    _In_ PDEVICE_OBJECT	DeviceObject,
    _In_ PIRP Irp,
    _In_ PKEVENT CompletionEvent
    )
{
  UNREFERENCED_PARAMETER(DeviceObject);
  UNREFERENCED_PARAMETER(CompletionEvent);

  IoFreeIrp(Irp);
  return STATUS_MORE_PROCESSING_REQUIRED;
}

NTSTATUS
NTAPI
KspAsyncContextWaitForCompletion(
  _In_ PKSOCKET_ASYNC_CONTEXT AsyncContext,
  _Inout_ PNTSTATUS Status
  )
{
  // TODO: accept a timeout value and make it configurable,
  // where applicable.
  if (*Status == STATUS_PENDING)
  {
    KeWaitForSingleObject(
      &AsyncContext->CompletionEvent,
      Executive,
      KernelMode,
      FALSE,
      NULL
    );

    *Status = AsyncContext->Irp->IoStatus.Status;
  }

  return *Status;
}

//////////////////////////////////////////////////////////////////////////
// Public functions.
//////////////////////////////////////////////////////////////////////////

NTSTATUS
NTAPI
KsInitialize(
  VOID
  )
{
  NTSTATUS Status;

  //
  // Register as a WSK client.
  //

  WSK_CLIENT_NPI WskClient;
  WskClient.ClientContext = NULL;
  WskClient.Dispatch      = &WskDispatch;

  if (WskInitialized) {
    // Already initialized.
    return STATUS_SUCCESS;
  }

  Status = WskRegister(&WskClient, &WskRegistration);
  if (!NT_SUCCESS(Status)) {
    return Status;
  }

  //
  // Capture the provider NPI.
  //

  Status = WskCaptureProviderNPI(
      &WskRegistration,
      WSK_INFINITE_WAIT,
      &WskProvider);
  if (!NT_SUCCESS(Status)) {
    WskDeregister(&WskRegistration);
    return Status;
  }

  WskInitialized = TRUE;
  return STATUS_SUCCESS;
}

VOID
NTAPI
KsDestroy(
  VOID
  )
{
  if (!WskInitialized) {
    // Nothing to do.
    return;
  }

  //
  // Release the provider NPI instance.
  //
  WskReleaseProviderNPI(&WskRegistration);

  //
  // Deregister as a WSK client.
  //
  WskDeregister(&WskRegistration);

  WskInitialized = FALSE;
}

NTSTATUS
NTAPI
KsGetAddrInfo(
  _In_opt_ PUNICODE_STRING NodeName,
  _In_opt_ PUNICODE_STRING ServiceName,
  _In_ PADDRINFOEXW Hints,
  _Out_ PADDRINFOEXW* Result
  )
{
  NTSTATUS Status;

  //
  // Allocate async context.
  //

  KSOCKET_ASYNC_CONTEXT AsyncContext;
  Status = KspAsyncContextAllocate(&AsyncContext);

  if (!NT_SUCCESS(Status))
  {
    return Status;
  }

  //
  // Call the WSK API.
  //

  Status = WskProvider.Dispatch->WskGetAddressInfo(
    WskProvider.Client,         // Client
    NodeName,                   // NodeName
    ServiceName,                // ServiceName
    0,                          // NameSpace
    NULL,                       // Provider
    Hints,                      // Hints
    Result,                     // Result
    NULL,                       // OwningProcess
    NULL,                       // OwningThread
    AsyncContext.Irp            // Irp
    );

  KspAsyncContextWaitForCompletion(&AsyncContext, &Status);

  //
  // Free the async context.
  //

  KspAsyncContextFree(&AsyncContext);

  return Status;
}

VOID
NTAPI
KsFreeAddrInfo(
  _In_ PADDRINFOEXW AddrInfo
  )
{
  WskProvider.Dispatch->WskFreeAddressInfo(
    WskProvider.Client,         // Client
    AddrInfo                    // AddrInfo
    );
}

NTSTATUS
NTAPI
KsCreateSocket(
  _Out_ PKSOCKET* Socket,
  _In_ ADDRESS_FAMILY AddressFamily,
  _In_ USHORT SocketType,
  _In_ ULONG Protocol,
  _In_ ULONG Flags
  )
{
  NTSTATUS Status;

  //
  // Allocate memory for the socket structure.
  //
  // PagedPool below will trigger the verifier 
  // Synchronization object address is bogus or pageable.
  PKSOCKET NewSocket = ExAllocatePoolWithTag(NonPagedPoolNx, sizeof(KSOCKET), MEMORY_TAG);

  if (!NewSocket)
  {
    return STATUS_INSUFFICIENT_RESOURCES;
  }

  //
  // Allocate async context for the socket.
  //
  Status = KspAsyncContextAllocate(&NewSocket->AsyncContextRead);

  if (!NT_SUCCESS(Status))
  {
    return Status;
  }

  Status = KspAsyncContextAllocate(&NewSocket->AsyncContextWrite);

  if (!NT_SUCCESS(Status))
  {
    return Status;
  }

  //
  // Create the WSK socket.
  //

  Status = WskProvider.Dispatch->WskSocket(
    WskProvider.Client,         // Client
    AddressFamily,              // AddressFamily
    SocketType,                 // SocketType
    Protocol,                   // Protocol
    Flags,                      // Flags
    NULL,                       // SocketContext
    NULL,                       // Dispatch
    NULL,                       // OwningProcess
    NULL,                       // OwningThread
    NULL,                       // SecurityDescriptor
    NewSocket->AsyncContextRead.Irp // Irp
    );

  KspAsyncContextWaitForCompletion(&NewSocket->AsyncContextRead, &Status);

  //
  // Save the socket instance and the socket dispatch table.
  //

  if (NT_SUCCESS(Status))
  {
    NewSocket->WskSocket = (PWSK_SOCKET)NewSocket->AsyncContextRead.Irp->IoStatus.Information;
    NewSocket->WskDispatch = (PVOID)NewSocket->WskSocket->Dispatch;
    NewSocket->operation = 0;
    *Socket = NewSocket;
  }

  return Status;
}

NTSTATUS
NTAPI
KsCreateConnectionSocket(
  _Out_ PKSOCKET* Socket,
  _In_ ADDRESS_FAMILY AddressFamily,
  _In_ USHORT SocketType,
  _In_ ULONG Protocol
  )
{
  return KsCreateSocket(Socket, AddressFamily, SocketType, Protocol, WSK_FLAG_CONNECTION_SOCKET);
}

NTSTATUS
NTAPI
KsCreateListenSocket(
  _Out_ PKSOCKET* Socket,
  _In_ ADDRESS_FAMILY AddressFamily,
  _In_ USHORT SocketType,
  _In_ ULONG Protocol
  )
{
  return KsCreateSocket(Socket, AddressFamily, SocketType, Protocol, WSK_FLAG_LISTEN_SOCKET);
}

NTSTATUS
NTAPI
KsCreateDatagramSocket(
  _Out_ PKSOCKET* Socket,
  _In_ ADDRESS_FAMILY AddressFamily,
  _In_ USHORT SocketType,
  _In_ ULONG Protocol
  )
{
  return KsCreateSocket(Socket, AddressFamily, SocketType, Protocol, WSK_FLAG_DATAGRAM_SOCKET);
}

NTSTATUS
NTAPI
KsDisconnectSocket(
    _In_ PKSOCKET Socket
)
{
  NTSTATUS Status = STATUS_UNSUCCESSFUL;

  KSOCKET_ASYNC_CONTEXT AsyncContext;
  Status = KspAsyncContextAllocate(&AsyncContext);

  if (!NT_SUCCESS(Status))
  {
    return Status;
  }

  //
  // Close the WSK socket.
  //
  Status = Socket->WskConnectionDispatch->WskDisconnect(
    Socket->WskSocket,
    NULL,
    0,
    AsyncContext.Irp
  );
  KspAsyncContextWaitForCompletion(&AsyncContext, &Status);
  KspAsyncContextFree(&AsyncContext);

  return Status;
}

NTSTATUS
NTAPI
KsCloseSocket(
  _In_ PKSOCKET Socket
  )
{
  NTSTATUS Status = STATUS_UNSUCCESSFUL;

  KSOCKET_ASYNC_CONTEXT AsyncContext;
  Status = KspAsyncContextAllocate(&AsyncContext);

  if (!NT_SUCCESS(Status))
  {
    goto Exit;
  }

  //
  // Close the WSK socket.
  //
  Status = Socket->WskConnectionDispatch->WskCloseSocket(
    Socket->WskSocket,
    AsyncContext.Irp
  );
  KspAsyncContextWaitForCompletion(&AsyncContext, &Status);
  //
  // Free the async context.
  //
  KspAsyncContextFree(&AsyncContext);

Exit:
  KspAsyncContextFree(&Socket->AsyncContextRead);
  KspAsyncContextFree(&Socket->AsyncContextWrite);

  //
  // Free memory for the socket structure.
  //

  ExFreePoolWithTag(Socket, MEMORY_TAG);

  return Status;
}

NTSTATUS
NTAPI
KsBind(
  _In_ PKSOCKET Socket,
  _In_ PSOCKADDR LocalAddress
  )
{
  NTSTATUS Status;

  //
  // Reset the async context.
  //

  KspAsyncContextReset(&Socket->AsyncContextRead);

  //
  // Bind the socket.
  //

  Status = Socket->WskListenDispatch->WskBind(
    Socket->WskSocket,          // Socket
    LocalAddress,               // LocalAddress
    0,                          // Flags (reserved)
    Socket->AsyncContextRead.Irp    // Irp
    );

  KspAsyncContextWaitForCompletion(&Socket->AsyncContextRead, &Status);

  return Status;
}

NTSTATUS
NTAPI
KsAccept(
  _In_ PKSOCKET Socket,
  _Out_ PKSOCKET* NewSocket,
  _Out_opt_ PSOCKADDR LocalAddress,
  _Out_opt_ PSOCKADDR RemoteAddress
  )
{
  NTSTATUS Status;

  //
  // Reset the async context.
  //

  KspAsyncContextReset(&Socket->AsyncContextRead);

  //
  // Accept the connection.
  //

  Status = Socket->WskListenDispatch->WskAccept(
    Socket->WskSocket,          // ListenSocket
    0,                          // Flags
    NULL,                       // AcceptSocketContext
    NULL,                       // AcceptSocketDispatch
    LocalAddress,               // LocalAddress
    RemoteAddress,              // RemoteAddress
    Socket->AsyncContextRead.Irp    // Irp
    );

  KspAsyncContextWaitForCompletion(&Socket->AsyncContextRead, &Status);

  //
  // Save the socket instance and the socket dispatch table.
  //

  if (NT_SUCCESS(Status))
  {
    PKSOCKET KNewSocket = ExAllocatePoolWithTag(PagedPool, sizeof(KSOCKET), MEMORY_TAG);

    if (!KNewSocket)
    {
      return STATUS_INSUFFICIENT_RESOURCES;
    }

    KNewSocket->WskSocket = (PWSK_SOCKET)Socket->AsyncContextRead.Irp->IoStatus.Information;
    KNewSocket->WskDispatch = (PVOID)KNewSocket->WskSocket->Dispatch;

    Status = KspAsyncContextAllocate(&KNewSocket->AsyncContextRead);

    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(KNewSocket, MEMORY_TAG);
        return Status;
    }

    Status = KspAsyncContextAllocate(&KNewSocket->AsyncContextWrite);

    if (!NT_SUCCESS(Status))
    {
      ExFreePoolWithTag(KNewSocket, MEMORY_TAG);
      return Status;
    }

    *NewSocket = KNewSocket;
  }

  return Status;
}

NTSTATUS
NTAPI
KsConnect(
  _In_ PKSOCKET Socket,
  _In_ PSOCKADDR RemoteAddress
  )
{
  NTSTATUS Status;

  //
  // Reset the async context.
  //

  KspAsyncContextReset(&Socket->AsyncContextRead);

  //
  // Bind the socket to the local address.
  //

  SOCKADDR_IN LocalAddress;
  LocalAddress.sin_family       = AF_INET;
  LocalAddress.sin_addr.s_addr  = INADDR_ANY;
  LocalAddress.sin_port         = 0;

  Status = Socket->WskConnectionDispatch->WskBind(
    Socket->WskSocket,          // Socket
    (PSOCKADDR)&LocalAddress,   // LocalAddress
    0,                          // Flags (reserved)
    Socket->AsyncContextRead.Irp    // Irp
    );

  KspAsyncContextWaitForCompletion(&Socket->AsyncContextRead, &Status);

  if (!NT_SUCCESS(Status))
  {
    return Status;
  }

  //
  // Reset the async context (again).
  //

  KspAsyncContextReset(&Socket->AsyncContextRead);

  //
  // Connect to the remote host.
  //
  // N.B.: Instead of calling WskSocket(), WskBind() and WskConnect(),
  // it is possible to just call WskSocketConnect().
  //

  Status = Socket->WskConnectionDispatch->WskConnect(
    Socket->WskSocket,          // Socket
    RemoteAddress,              // RemoteAddress
    0,                          // Flags (reserved)
    Socket->AsyncContextRead.Irp    // Irp
    );

  KspAsyncContextWaitForCompletion(&Socket->AsyncContextRead, &Status);

  return Status;
}

NTSTATUS
NTAPI
KsSendRecv(
  _In_ PKSOCKET Socket,
  _In_ PVOID Buffer,
  _Inout_ PULONG Length,
  _In_ ULONG Flags,
  _In_ BOOLEAN Send
  )
{
  NTSTATUS Status;

  //
  // Wrap the buffer into the "WSK buffer".
  //

  WSK_BUF WskBuffer;
  WskBuffer.Offset  = 0;
  WskBuffer.Length  = *Length;
  WskBuffer.Mdl     = IoAllocateMdl(Buffer, (ULONG)WskBuffer.Length, FALSE, FALSE, NULL);

  if (NULL == WskBuffer.Mdl) {
    Status = STATUS_INSUFFICIENT_RESOURCES;
    goto Error;
  }

  __try
  {
    MmProbeAndLockPages(WskBuffer.Mdl, KernelMode, IoWriteAccess);
  }
  __except (EXCEPTION_EXECUTE_HANDLER)
  {
    Status = STATUS_ACCESS_VIOLATION;
    goto Error;
  }

  //
  // Send / receive the data.
  //
  InterlockedIncrement(&Socket->operation);
  if (Send)
  {
    KspAsyncContextReset(&Socket->AsyncContextWrite);
    Status = Socket->WskConnectionDispatch->WskSend(
      Socket->WskSocket,        // Socket
      &WskBuffer,               // Buffer
      Flags,                    // Flags
      Socket->AsyncContextWrite.Irp  // Irp
      );
    KspAsyncContextWaitForCompletion(&Socket->AsyncContextWrite, &Status);
  }
  else
  {
    KspAsyncContextReset(&Socket->AsyncContextRead);
    Status = Socket->WskConnectionDispatch->WskReceive(
      Socket->WskSocket,        // Socket
      &WskBuffer,               // Buffer
      Flags,                    // Flags
      Socket->AsyncContextRead.Irp  // Irp
      );
    KspAsyncContextWaitForCompletion(&Socket->AsyncContextRead, &Status);
  }

  InterlockedDecrement(&Socket->operation);
  //
  // Set the number of bytes sent / received.
  //

  if (NT_SUCCESS(Status))
  {
    if (Send) {
      *Length = (ULONG)Socket->AsyncContextWrite.Irp->IoStatus.Information;
    }
    else {
      *Length = (ULONG)Socket->AsyncContextRead.Irp->IoStatus.Information;
    }
  }

  //
  // Free the MDL.
  //

  MmUnlockPages(WskBuffer.Mdl);

Error:
  if (WskBuffer.Mdl) {
    IoFreeMdl(WskBuffer.Mdl);
  }
  return Status;
}

NTSTATUS
NTAPI
KsSendRecvUdp(
  _In_ PKSOCKET Socket,
  _In_ PVOID Buffer,
  _Inout_ PULONG Length,
  _In_ ULONG Flags,
  _In_ PSOCKADDR RemoteAddress,
  _In_ BOOLEAN Send
  )
{
  NTSTATUS Status;

  //
  // Wrap the buffer into the "WSK buffer".
  //

  WSK_BUF WskBuffer;
  WskBuffer.Offset  = 0;
  WskBuffer.Length  = *Length;
  WskBuffer.Mdl     = IoAllocateMdl(Buffer, (ULONG)WskBuffer.Length, FALSE, FALSE, NULL);

  if (NULL == WskBuffer.Mdl) {
      Status = STATUS_INSUFFICIENT_RESOURCES;
      goto Error;
  }
  __try
  {
    MmProbeAndLockPages(WskBuffer.Mdl, KernelMode, IoWriteAccess);
  }
  __except (EXCEPTION_EXECUTE_HANDLER)
  {
    Status = STATUS_ACCESS_VIOLATION;
    goto Error;
  }

  //
  // Send / receive the data.
  //

  if (Send)
  {
    KspAsyncContextReset(&Socket->AsyncContextWrite);
    Status = Socket->WskDatagramDispatch->WskSendTo(
      Socket->WskSocket,        // Socket
      &WskBuffer,               // Buffer
      Flags,                    // Flags (reserved)
      RemoteAddress,            // RemoteAddress
      0,                        // ControlInfoLength
      NULL,                     // ControlInfo
      Socket->AsyncContextWrite.Irp  // Irp
      );
    KspAsyncContextWaitForCompletion(&Socket->AsyncContextWrite, &Status);
  }
  else
  {
    //
    // Use #pragma prefast (suppress: ...), because SAL annotation is wrong
    // for this function.
    //
    // From MSDN:
    //   ControlLength
    //   ControlInfo
    //
    //   ... This pointer is optional and can be NULL.  If the ControlInfoLength
    //   parameter is NULL, the ControlInfo parameter should be NULL.
    //
    KspAsyncContextReset(&Socket->AsyncContextRead);
#pragma prefast (                                                                           \
    suppress:__WARNING_INVALID_PARAM_VALUE_1,                                               \
    "If the ControlInfoLength parameter is NULL, the ControlInfo parameter should be NULL." \
    )

#pragma warning(disable:6387)
    Status = Socket->WskDatagramDispatch->WskReceiveFrom(
      Socket->WskSocket,        // Socket
      &WskBuffer,               // Buffer
      Flags,                    // Flags (reserved)
      RemoteAddress,            // RemoteAddress
      NULL,                     // ControlInfoLength
      NULL,                     // ControlInfo
      NULL,                     // ControlFlags
      Socket->AsyncContextRead.Irp  // Irp
      );
    KspAsyncContextWaitForCompletion(&Socket->AsyncContextRead, &Status);
#pragma warning(default:6387)
  }

  //
  // Set the number of bytes sent / received.
  //

  if (NT_SUCCESS(Status))
  {
      if (Send)
      {
        *Length = (ULONG)Socket->AsyncContextWrite.Irp->IoStatus.Information;
      }
      else
      {
        *Length = (ULONG)Socket->AsyncContextRead.Irp->IoStatus.Information;
      }
  }

  //
  // Free the MDL.
  //

  MmUnlockPages(WskBuffer.Mdl);

Error:
  if (WskBuffer.Mdl) {
    IoFreeMdl(WskBuffer.Mdl);
  }
  return Status;
}

NTSTATUS
NTAPI
KsSend(
  _In_ PKSOCKET Socket,
  _In_ PVOID Buffer,
  _Inout_ PULONG Length,
  _In_ ULONG Flags
  )
{
  return KsSendRecv(Socket, Buffer, Length, Flags, TRUE);
}

NTSTATUS
NTAPI
KsRecv(
  _In_ PKSOCKET Socket,
  _In_ PVOID Buffer,
  _Inout_ PULONG Length,
  _In_ ULONG Flags
  )
{
  return KsSendRecv(Socket, Buffer, Length, Flags, FALSE);
}

NTSTATUS
NTAPI
KsSendTo(
  _In_ PKSOCKET Socket,
  _In_ PVOID Buffer,
  _Inout_ PULONG Length,
  _In_ ULONG Flags,
  _In_ PSOCKADDR RemoteAddress
  )
{
  return KsSendRecvUdp(Socket, Buffer, Length, Flags, RemoteAddress, TRUE);
}

NTSTATUS
NTAPI
KsRecvFrom(
  _In_ PKSOCKET Socket,
  _In_ PVOID Buffer,
  _Inout_ PULONG Length,
  _In_ ULONG Flags,
  _In_ PSOCKADDR RemoteAddress
  )
{
  return KsSendRecvUdp(Socket, Buffer, Length, Flags, RemoteAddress, FALSE);
}
