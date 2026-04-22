from invoke import task
from tasks.lib import build_faasm_lib


@task(default=True)
def build(ctx, clean=False, native=False, shared=False):
    """
    Builds Faabric RPC lib
    """
    build_faasm_lib("libfaasrpc", clean=clean, native=native, shared=shared)
