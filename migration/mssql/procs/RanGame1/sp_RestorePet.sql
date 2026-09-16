

-- Return (nReturn)
-- DB_ERROR -1
-- DB_OK 0

-- Return (nPetNum)

CREATE PROCEDURE [dbo].[sp_RestorePet]
	@nPetNum	int,
	@nPetChaNum	int,
	@nReturn	int OUTPUT
As
	DECLARE
		@error_var int, 
		@rowcount_var int

	SET NOCOUNT ON
	SET @nReturn = 0

	BEGIN TRAN

	Update PetInfo Set PetDeleted=0, PetFull=1000, PetPutOnItems=''
	Where PetNum=@nPetNum And PetChaNum=@nPetChaNum And PetDeleted=1
	
	SELECT @error_var = @@ERROR, @rowcount_var = @@ROWCOUNT    
	IF @error_var <> 0 OR @rowcount_var = 0
	BEGIN
		ROLLBACK TRAN
		SET @nReturn = -1
	END
	ELSE
	BEGIN
		COMMIT TRAN
		SET @nReturn = @nPetNum
	END

	SET NOCOUNT OFF
	RETURN @nReturn


